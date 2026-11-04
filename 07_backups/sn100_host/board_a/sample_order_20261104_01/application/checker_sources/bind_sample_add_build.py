"""只读核对本次完整构建，生成专项链接清单；不运行产品或连接RK。"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import tarfile

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent


def require(ok, reason):
    if not ok:
        raise RuntimeError(reason)


def sha(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def regular(path):
    path = Path(path)
    require(path.is_file() and not path.is_symlink(), "需要普通文件：" + str(path))
    return path


def recorded(record):
    name = record["path"]
    rel = PurePosixPath(name)
    require(not rel.is_absolute() and ".." not in rel.parts and "\\" not in name, "记录路径越界")
    path = regular(ROOT / name)
    require(path.resolve().is_relative_to(ROOT), "记录目标越界")
    require(path.stat().st_size == record["bytes"] and sha(path) == record["sha256"], "文件不等于绑定记录：" + name)
    return path


def arm64(path, relocatable=False):
    with regular(path).open("rb") as stream:
        header = stream.read(64)
    require(len(header) == 64 and header[:6] == b"\x7fELF\x02\x01" and int.from_bytes(header[18:20], "little") == 183,
            "文件不是ARM64小端ELF：" + str(path))
    require(int.from_bytes(header[16:18], "little") in ({1} if relocatable else {2, 3}), "ELF类型不符")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("build_directory")
    parser.add_argument("release")
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    require(re.fullmatch(r"[a-f0-9]{64}", args.release), "发布编号无效")
    build = Path(args.build_directory)
    require(build.is_absolute() and build.resolve() == build and build.is_relative_to(ROOT / "temp/sn100_host"), "构建目录必须在本项目temp内且不重定向")
    require(regular(build / "build_exit.txt").read_text().strip() == "0", "本次完整构建未实际退出0")
    lock = regular(build / "arm64_build/build.lock")
    fd = int(os.environ["SN100_SAMPLE_ADD_LOCK_FD"])
    actual, expected = os.fstat(fd), lock.stat()
    require((actual.st_dev, actual.st_ino) == (expected.st_dev, expected.st_ino), "没有继承同一次正式构建锁")
    binding_path = regular(build / "object_binding.json")
    binding = json.loads(binding_path.read_text(encoding="utf-8"))
    require(binding["mapping_basis"] == "reconstructed_from_frozen_build_rules_and_actual_tsv", "不是已经核对的完整构建对象绑定")
    snapshot_path = recorded(binding["source_snapshot"])
    require(snapshot_path == build / "source_snapshot.json", "来源快照不属于本次完整构建")
    sources = json.loads(snapshot_path.read_text(encoding="utf-8"))
    require(sources == binding["source_files"] and "samples/sample_page.cpp" in sources and "shell/main.cpp" in sources, "来源清单与对象绑定不一致")
    program = ROOT / "06_programs/sn100_host"
    for name, digest in sources.items():
        rel = PurePosixPath(name)
        require(not rel.is_absolute() and ".." not in rel.parts, "来源路径越界")
        require(sha(regular(program / name)) == digest, "正式源码在构建后变化：" + name)
    objects = binding["link_objects"]["main"]
    require(objects and len(objects) == binding["object_counts"]["main"], "主程序对象清单不完整")
    paths = [recorded(item) for item in objects]
    require(len(set(paths)) == len(paths) and all(path.parent == build / "arm64_build" for path in paths), "对象重复或不属于本次完整构建")
    for path in paths:
        arm64(path, relocatable=True)
    main_objects = [item for item in binding["objects"] if item["source"]["path"].endswith("/shell/main.cpp") and "main" in item["link_roles"]]
    require(len(main_objects) == 1 and main_objects[0]["object"] in objects, "真实main对象必须且只能有一个")
    binary = recorded(binding["binaries"]["sn100_shell"])
    require(binary == build / "build/sn100_shell", "主程序不属于本次构建")
    arm64(binary)
    package = Path(regular(build / "packages/latest_package").read_text().strip())
    require(package.parent == build / "packages" and package.name == "sn100_shell_" + args.release + ".tar.gz", "发布包与本次构建/发布号不一致")
    with tarfile.open(regular(package), "r:gz") as bundle:
        names = [item for item in bundle.getmembers() if item.name == "release_id"]
        mains = [item for item in bundle.getmembers() if item.name == "sn100_shell"]
        require(len(names) == len(mains) == 1 and bundle.extractfile(names[0]).read().decode("ascii").strip() == args.release, "包内发布号不符")
        require(hashlib.sha256(bundle.extractfile(mains[0]).read()).hexdigest() == sha(binary), "包内主程序与同次完整构建不一致")
    record = {"schema": "sn100-sample-add-checker-binding-v1", "release_id": args.release,
              "build_directory": str(build), "binding_sha256": sha(binding_path),
              "source_snapshot_sha256": sha(snapshot_path), "main_binary_sha256": sha(binary),
              "package_sha256": sha(package), "object_count": len(objects), "main_object": main_objects[0]["object"],
              "checker_source_sha256": sha(HERE / "sample_add_review.cpp"),
              "object_records": objects, "runtime_verified": False}
    out = HERE / "build_01"
    if args.verify:
        previous = json.loads(regular(out / "binding.json").read_text(encoding="utf-8"))
        require(previous == record, "专项编译前后来源或对象有变化")
    else:
        require(not (out / "binding.json").exists() and not (out / "link_objects.txt").exists(), "已有本轮构建证据，不覆盖重跑")
        with (out / "binding.json").open("x", encoding="utf-8", newline="\n") as stream:
            json.dump(record, stream, ensure_ascii=False, indent=2)
            stream.write("\n")
        with (out / "link_objects.txt").open("x", encoding="utf-8", newline="\n") as stream:
            stream.write("\n".join(str(path) for path in paths) + "\n")
    print("SAMPLE_ADD_COMPLETE_BUILD_BINDING_VERIFIED=1")


if __name__ == "__main__":
    main()
