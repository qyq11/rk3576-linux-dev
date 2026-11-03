"""把第07批冻结晋级、实际构建输入及ARM64包逐份对上；不运行产品。"""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import json
import os
import re

from integration_07_tool_support import (
    TASK, ROOT, WORK, PROGRAM, cfg, local_path, promotion, read_json,
    require_regular, sha, source_snapshot, write_new,
)


def raw(path, within=ROOT):
    require_regular(path, within=within)
    return path.read_bytes()


def safe_name(name):
    if (not isinstance(name, str) or not name or "\\" in name or ":" in name
            or PurePosixPath(name).is_absolute() or any(ord(char) < 32 for char in name)
            or any(part in ("", ".", "..") for part in name.split("/"))):
        raise RuntimeError("清单中的项目相对路径无效。")
    return name


def save_bytes_once(path, content):
    if path.exists() or path.is_symlink():
        if raw(path, WORK) != content:
            raise RuntimeError("已有本批证据不同，保留原件并停止：" + str(path))
        return
    with path.open("xb") as output:
        output.write(content)
        output.flush()
        os.fsync(output.fileno())


def save_json_once(path, value):
    if path.exists() or path.is_symlink():
        raw(path, WORK)
        if read_json(path) != value:
            raise RuntimeError("已有本批核查结果不同，保留原件并停止：" + str(path))
        return
    write_new(path, value)


def arm64_elf(path, expected_types):
    content = raw(path, WORK)
    if (len(content) < 64 or content[:6] != b"\x7fELF\x02\x01"
            or int.from_bytes(content[18:20], "little") != 183
            or int.from_bytes(content[16:18], "little") not in expected_types):
        raise RuntimeError("不是本批所需的ARM64小端ELF：" + str(path))
    return hashlib.sha256(content).hexdigest()


def check_source_and_inputs(config, progress, expected):
    snapshot_path = WORK / "source_snapshot.json"
    snapshot_raw = raw(snapshot_path, WORK)
    snapshot = json.loads(snapshot_raw)
    if not expected or snapshot != expected or progress.get("final_source_files") != expected:
        raise RuntimeError("实际源码快照与冻结07晋级终态不一致。")
    for name, wanted in expected.items():
        path = PROGRAM / safe_name(name)
        if not re.fullmatch(r"[a-f0-9]{64}", wanted) or hashlib.sha256(raw(path, PROGRAM)).hexdigest() != wanted:
            raise RuntimeError("冻结源码已变化：" + name)
    baseline = read_json(ROOT / safe_name(config["baseline_snapshot"]["path"]))
    if not isinstance(baseline, dict):
        raise RuntimeError("冻结底稿不是相对项目路径到SHA的清单。")
    for name, wanted in baseline.items():
        safe_name(name)
        if not isinstance(wanted, str) or not re.fullmatch(r"[a-f0-9]{64}", wanted):
            raise RuntimeError("冻结底稿指纹格式无效。")

    kinds = {"source", "form", "header", "resource", "helper_source"}
    counts, checked, rows = {}, set(), {kind: [] for kind in kinds}
    for line in raw(WORK / "build_inputs.tsv", WORK).decode("utf-8").splitlines():
        if "\t" not in line:
            raise RuntimeError("构建输入行缺少类型或路径。")
        kind, value = line.split("\t", 1)
        if kind == "include":
            continue
        if kind not in kinds:
            raise RuntimeError("未知构建输入类型：" + kind)
        path = Path(value)
        if not path.is_absolute():
            raise RuntimeError("实际构建输入不是绝对路径。")
        name = path.relative_to(PROGRAM).as_posix()
        if name not in expected or hashlib.sha256(raw(path, PROGRAM)).hexdigest() != expected[name]:
            raise RuntimeError("实际构建输入未被冻结源码覆盖：" + name)
        counts[kind] = counts.get(kind, 0) + 1
        rows[kind].append(path)
        checked.add(name)
    if set(counts) != kinds or "shell/main.cpp" not in checked or "history/report_renderer.cpp" not in checked:
        raise RuntimeError("整程序五类输入或主程序/报告入口不完整。")
    progress_path = ROOT / safe_name(config["promotion_progress"]["path"])
    progress_raw = raw(progress_path)
    if hashlib.sha256(progress_raw).hexdigest() != config["promotion_progress"]["sha256"]:
        raise RuntimeError("读取构建输入期间，冻结晋级记录已变化。")
    added = sorted(set(expected) - set(baseline))
    evidence = {
        "format": "sn100-integration-07-inputs-v1",
        "source_count": len(expected), "unique_build_inputs": len(checked), "kinds": counts,
        "source_snapshot_sha256": hashlib.sha256(snapshot_raw).hexdigest(),
        "tools_config_sha256": sha(TASK / "integration_07_tools_config.json"),
        "promotion_progress_sha256": hashlib.sha256(progress_raw).hexdigest(),
        "final_merge_review_sha256": config["final_review"]["sha256"],
        "patch_count": len(config["patches"]), "added_files": added, "added_files_count": len(added),
        "deleted_files": sorted(set(baseline) - set(expected)),
        "program_executed": False, "board_runtime_verified": False,
    }
    save_bytes_once(WORK / "merge_inputs.json", progress_raw)
    save_json_once(WORK / "input_verification.json", evidence)
    return evidence, rows


def check_objects(rows):
    build = WORK / "arm64_build"
    # 与冻结build.sh的实际生成规则一致：每个普通源、Q_OBJECT头及QRC各编一个主对象。
    moc_count = sum(b"Q_OBJECT" in raw(path, PROGRAM) for path in rows["header"])
    expected_main = len(rows["source"]) + moc_count + len(rows["resource"])
    main_sources = set(rows["source"])
    helper_sources = {path for path in rows["helper_source"] if path not in main_sources}
    expected_names = {"obj_" + str(index) + ".o" for index in range(expected_main)}
    expected_names.update("helper_" + path.stem + ".o" for path in helper_sources)
    objects = {path.name: path for path in build.glob("*.o")}
    if not expected_main or len(expected_names) != expected_main + len(helper_sources) or set(objects) != expected_names:
        raise RuntimeError("本批对象数量或名称与冻结构建输入推导不一致。")
    hashes = {name: arm64_elf(path, {1}) for name, path in sorted(objects.items())}
    return {
        "total": len(objects), "main": expected_main, "helper": len(helper_sources),
        "moc": moc_count, "resource": len(rows["resource"]), "aarch64": len(hashes),
    }


def check_package(config, expected, evidence, rows):
    binaries = {name: arm64_elf(WORK / "build" / name, {2, 3})
                for name in ("sn100_shell", "sn100_report_renderer")}
    dependency_path = WORK / "dependencies/dependency_report.json"
    dependency_raw = raw(dependency_path, WORK)
    dependency = json.loads(dependency_raw)
    if (dependency.get("errors") != [] or dependency.get("offline_link_and_sql_closure_passed") is not True
            or dependency.get("board_runtime_verified") is not False):
        raise RuntimeError("本批ARM64静态依赖核查没有完整通过。")
    for role, name in (("main", "sn100_shell"), ("report_renderer", "sn100_report_renderer")):
        matches = [item for item in dependency.get("files", []) if role in item.get("roles", [])]
        if len(matches) != 1 or matches[0].get("sha256") != binaries[name]:
            raise RuntimeError("依赖报告不是本批实际主程序/报告程序。")
    packages = WORK / "packages"
    package = Path(raw(packages / "latest_package", WORK).decode("utf-8").strip())
    if not package.is_absolute():
        raise RuntimeError("打包结果不是绝对路径。")
    package.resolve().relative_to(packages.resolve())
    match = re.fullmatch(r"sn100_shell_([a-f0-9]{64})\.tar\.gz", package.name)
    if not match:
        raise RuntimeError("程序包文件名或发布编号无效。")
    require_regular(package, within=packages)
    objects = check_objects(rows)
    result = dict(evidence)
    result.update({
        "format": "sn100-integration-07-build-complete-v1", "arm64_build_and_package_passed": True,
        "binaries": binaries, "release_id": match.group(1), "package": package.name,
        "package_sha256": sha(package), "dependency_report_sha256": hashlib.sha256(dependency_raw).hexdigest(),
        "objects": objects, "object_count": objects["total"], "aarch64_objects": objects["aarch64"],
    })
    # 再核冻结记录和全部正式源码，不能以构建前的旧结果代表打包结束时输入仍相同。
    if cfg() != config or source_snapshot() != expected or promotion().get("final_source_files") != expected:
        raise RuntimeError("打包收尾时晋级终态发生变化。")
    for name, wanted in expected.items():
        if hashlib.sha256(raw(PROGRAM / safe_name(name), PROGRAM)).hexdigest() != wanted:
            raise RuntimeError("打包结束时冻结源码已变化：" + name)
    save_json_once(WORK / "build_complete.json", result)
    print("ARM64_INTEGRATION_07_ARTIFACTS_VERIFIED=1")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--after-package", action="store_true")
    args = parser.parse_args()
    config = cfg()
    progress = promotion()
    if progress.get("complete") is not True or progress.get("phase") != "complete" or progress.get("current_patch") is not None:
        raise RuntimeError("第07批晋级未完整完成。")
    expected = source_snapshot()
    evidence, rows = check_source_and_inputs(config, progress, expected)
    print("SOURCE_FILES_COVERED=" + str(evidence["source_count"]))
    print("BUILD_INPUTS_COVERED_BY_SOURCE_SNAPSHOT=" + str(evidence["unique_build_inputs"]))
    if args.after_package:
        check_package(config, expected, evidence, rows)


if __name__ == "__main__":
    main()
