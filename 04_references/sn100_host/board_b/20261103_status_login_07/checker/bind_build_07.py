#!/usr/bin/env python3
"""把07检查器绑定到最终发布、归档源码和同一次正式构建的完整对象。

只检查文件或写链接清单，不运行产品。单独调用也必须取得正式构建锁；
build_review_07.sh 会把同一个锁的文件描述符传给本工具。
"""

import argparse
import contextlib
import hashlib
import json
import os
import re
import stat
import struct
from pathlib import Path, PurePosixPath


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
SCHEMA = "sn100-review-07-v1"
REJECTED_RELEASES = {
    "14ce9714aaa0017c7ad78ff3f551d978672e5def79b96e817605295d7e59020c",
    "7c4abe3062ead1f5b6fc6fd495b708be5f11436504bba5e900b6ccb2c4f5d5f9",
    "e31491a11e2e51764ab49c2c4435ce2f6f1f136223a76323d508ed613a758270",
}
TEMPLATE_NAMES = (
    "freeze_release_07.py", "bind_build_07.py", "build_review_07.sh",
    "shutdown_contract_07.py", "status_login_review_07.cpp", "ad_wire_observer.h",
    "run_case_on_board.sh", "capture_application_exit.sh", "summarize_run.py",
)
CONTRACT_NAMES = (
    "shutdown_contract.json", "required_shutdown_markers.txt",
    "forbidden_shutdown_markers.txt", "shutdown_contract_release_id",
)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def regular(path):
    path = Path(path)
    require(path.is_file() and not path.is_symlink(), "需要普通文件，不能是链接：" + str(path))
    return path


def sha(path):
    h = hashlib.sha256()
    with regular(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def read_json(path):
    return json.loads(regular(path).read_text(encoding="utf-8"))


def canonical_json(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")


def new_text(path, value):
    path = Path(path)
    require(not path.exists() and not path.is_symlink(), "证据已存在，不能覆盖：" + str(path))
    with path.open("x", encoding="utf-8", newline="\n") as stream:
        stream.write(value)


def new_json(path, value):
    new_text(path, json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n")


def validate_release_id(value):
    require(re.fullmatch(r"[a-f0-9]{64}", value) is not None, "发布编号必须是完整的小写SHA-256")
    require(value not in REJECTED_RELEASES, "拒绝旧06/05/7c发布及其对象")
    return value


def integration_path(value):
    path = Path(value)
    require(path.is_absolute(), "最终07构建目录必须是绝对路径")
    resolved = path.resolve(strict=True)
    require(resolved == path and resolved.name == "integration_07", "只接受未重定向的最终integration_07目录")
    require(resolved.is_relative_to(ROOT / "temp/sn100_host"), "最终07构建目录不在本项目temp/sn100_host内")
    require(not any(part in {"integration_05", "frozen_05"} for part in resolved.parts), "不能混入05目录")
    return resolved


@contextlib.contextmanager
def build_lock(integration):
    """与正式shell/build.sh使用同一个锁，禁止构建中读取半批对象。"""
    import fcntl

    lock = regular(Path(integration) / "arm64_build/build.lock")
    require(lock.resolve() == lock, "正式构建锁不能经过目录链接")
    inherited = os.environ.get("SN100_07_LOCK_FD")
    owned = inherited is None
    if owned:
        fd = os.open(str(lock), os.O_RDWR | os.O_NOFOLLOW)
    else:
        require(re.fullmatch(r"[0-9]+", inherited) is not None, "继承的构建锁描述符无效")
        fd = int(inherited)
        require(Path(os.readlink("/proc/self/fd/" + str(fd))) == lock, "继承的锁不是这次06构建锁")
        actual, expected = os.fstat(fd), lock.stat()
        require((actual.st_dev, actual.st_ino) == (expected.st_dev, expected.st_ino), "构建锁文件已被替换")
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        yield
    finally:
        if owned:
            os.close(fd)


def relative_name(name):
    require(isinstance(name, str) and name and "\\" not in name and "\x00" not in name,
            "源码清单含无效路径")
    path = PurePosixPath(name)
    require(not path.is_absolute() and all(p not in {"", ".", ".."} for p in name.split("/")),
            "源码清单路径越界：" + name)
    require(str(path) == name, "源码路径不是规范相对路径：" + name)
    return name


def validate_snapshot(snapshot):
    require(isinstance(snapshot, dict) and snapshot, "最终源码清单为空或格式错误")
    for name, digest in snapshot.items():
        relative_name(name)
        require(isinstance(digest, str) and re.fullmatch(r"[a-f0-9]{64}", digest), "源码指纹无效：" + name)
    require("shell/main.cpp" in snapshot and "shell/build.sh" in snapshot, "最终清单缺少真实入口或构建脚本")
    return snapshot


def read_rows(integration):
    rows = []
    for number, line in enumerate(regular(integration / "build_inputs.tsv").read_text(encoding="utf-8").splitlines(), 1):
        fields = line.split("\t")
        require(len(fields) == 2 and fields[0] in {"source", "header", "form", "resource", "include", "helper_source"},
                "build_inputs.tsv有未知或损坏行：" + str(number))
        require(fields[1] and "\x00" not in fields[1], "构建输入为空")
        rows.append(tuple(fields))
    mains = [value for kind, value in rows if kind == "source" and value.endswith("/shell/main.cpp")]
    require(len(mains) == 1, "必须只有一个真实shell/main.cpp来源")
    source_program = PurePosixPath(mains[0]).parents[1]
    require(source_program.is_absolute() and source_program.name == "sn100_host", "无法确认实际source_program")
    require(not any(p in {"integration_05", "frozen_05", "sn100_host_reference"} for p in source_program.parts),
            "实际源码根不能来自05或参考工程")
    return rows, source_program


def row_relative(value, source_program):
    require("\\" not in value and "\x00" not in value, "构建输入不是有效Linux路径")
    path = PurePosixPath(value)
    require(path.is_absolute() and ".." not in path.parts, "构建输入路径不规范：" + value)
    try:
        return relative_name(str(path.relative_to(source_program)))
    except ValueError as exc:
        raise RuntimeError("构建输入不属于最终实际source_program：" + value) from exc


def validate_rows(rows, source_program, snapshot):
    for kind, value in rows:
        if kind != "include":
            name = row_relative(value, source_program)
            require(name in snapshot, "最终源码归档缺少构建输入：" + name)
    for kind in ("source", "header", "form", "resource"):
        values = [value for row_kind, value in rows if row_kind == kind]
        require(len(values) == len(set(values)), "构建输入重复：" + kind)
    require(any(kind == "source" for kind, _ in rows), "正式源文件列表为空")


def verify_stage(integration, release_id):
    validate_release_id(release_id)
    require(regular(integration / "build_exit.txt").read_text().strip() == "0", "06正式构建没有真实exit0")
    snapshot_path = regular(integration / "source_snapshot.json")
    snapshot = validate_snapshot(read_json(snapshot_path))
    rows, source_program = read_rows(integration)
    validate_rows(rows, source_program, snapshot)
    packages = integration / "packages"
    stage_text = regular(packages / "latest_release").read_text().strip()
    stage = Path(stage_text)
    require(stage.is_absolute() and stage.resolve(strict=True) == stage and stage.is_relative_to(packages),
            "最终发布stage路径越界或被重定向")
    require(regular(stage / "release_id").read_text().strip() == release_id, "stage发布编号不匹配")
    require(sha(stage / "SHA256SUMS") == release_id, "发布编号与stage清单不一致")
    release = read_json(stage / "release.json")
    require(release.get("sources") == snapshot, "stage包的sources不等于最终源码清单")
    seen = set()
    for line in regular(stage / "SHA256SUMS").read_text().splitlines():
        match = re.fullmatch(r"([a-f0-9]{64})  (.+)", line)
        require(match is not None, "stage SHA256SUMS格式错误")
        digest, name = match.groups()
        relative_name(name)
        require(name not in seen, "stage清单重复：" + name)
        item = stage / name
        require(item.resolve(strict=True).is_relative_to(stage), "stage文件越界")
        require(sha(item) == digest, "stage文件指纹不匹配：" + name)
        seen.add(name)
    require({"sn100_shell", "release.json"}.issubset(seen), "stage清单缺少主程序或发布说明")
    app_sha = sha(stage / "sn100_shell")
    require(app_sha == sha(integration / "build/sn100_shell") == release.get("binary_sha256"),
            "06正式主程序、包和release.json指纹不匹配")
    return {
        "release_id": release_id, "integration_directory": str(integration),
        "stage_directory": str(stage), "application_sha256": app_sha,
        "stage_manifest_sha256": sha(stage / "SHA256SUMS"),
        "release_json_sha256": sha(stage / "release.json"),
        "build_inputs_sha256": sha(integration / "build_inputs.tsv"),
        "formal_snapshot_sha256": sha(snapshot_path), "source_program": str(source_program),
        "source_count": len(snapshot), "main_sha256": snapshot["shell/main.cpp"],
    }, snapshot, rows, source_program


def verify_frozen(frozen, snapshot):
    require(frozen.is_dir() and frozen.resolve() == frozen, "冻结源码目录缺失或经过链接")
    actual = set()
    for item in frozen.rglob("*"):
        require(not item.is_symlink(), "冻结源码不能含链接：" + str(item))
        if item.is_file():
            name = item.relative_to(frozen).as_posix()
            require(name in snapshot and sha(item) == snapshot[name], "冻结源码不属于最终归档：" + name)
            actual.add(name)
    require(actual == set(snapshot), "冻结源码不完整")


def elf_arm64(path, relocatable=False):
    with regular(path).open("rb") as stream:
        header = stream.read(20)
    require(len(header) == 20 and header[:6] == b"\x7fELF\x02\x01", "不是64位小端ELF：" + str(path))
    kind, machine = struct.unpack("<HH", header[16:20])
    require(machine == 183 and (not relocatable or kind == 1), "不是ARM64正式对象：" + str(path))


def object_binding(integration, frozen, rows, source_program):
    """严格按正式build.sh的source、moc、qrc顺序恢复连续obj_N。"""
    entries = []
    generated = set()
    for kind, value in rows:
        if kind == "source":
            entries.append({"kind": "source", "input": row_relative(value, source_program)})
    for kind, value in rows:
        if kind == "header":
            name = row_relative(value, source_program)
            if b"Q_OBJECT" in regular(frozen / name).read_bytes():
                generated_name = "moc_" + PurePosixPath(name).stem + ".cpp"
                require(generated_name not in generated, "moc文件名冲突，需人工核对正式构建")
                generated.add(generated_name)
                entries.append({"kind": "moc", "input": name, "generated": generated_name})
    resource_count = sum(kind == "resource" for kind, _ in rows)
    build_text = regular(frozen / "shell/build.sh").read_text(encoding="utf-8")
    names = []
    for match in re.finditer(r"(?m)^resource_names\+?=\(([^)]*)\)", build_text):
        require(re.fullmatch(r"[A-Za-z0-9_\s]*", match[1]) is not None, "资源生成名称改为动态表达式，需审查")
        names.extend(match[1].split())
    require(len(names) == resource_count and len(set(names)) == len(names), "资源对象顺序无法从最终build.sh确认")
    resource_rows = [value for kind, value in rows if kind == "resource"]
    for name, value in zip(names, resource_rows):
        generated_name = "qrc_" + name + ".cpp"
        generated.add(generated_name)
        entries.append({"kind": "resource", "input": row_relative(value, source_program), "generated": generated_name})
    build_dir = integration / "arm64_build"
    require(build_dir.resolve() == build_dir, "不能读取外部或05对象目录")
    actual_objects = list(build_dir.glob("obj_*.o"))
    expected = ["obj_" + str(i) + ".o" for i in range(len(entries))]
    require({p.name for p in actual_objects} == set(expected) and len(actual_objects) == len(expected),
            "正式对象缺失、多余或不再连续；拒绝沿用旧批次对象")
    objects = []
    for index, entry in enumerate(entries):
        path = build_dir / expected[index]
        require(path.resolve() == path, "正式对象被重定向：" + str(path))
        elf_arm64(path, relocatable=True)
        objects.append(dict(entry, index=index, path=str(path), sha256=sha(path)))
    for kind, value in rows:
        if kind == "form":
            generated.add("ui_" + PurePosixPath(value).stem + ".h")
    generated_hashes = {name: sha(build_dir / name) for name in sorted(generated)}
    mains = [o for o in objects if o["kind"] == "source" and o["input"] == "shell/main.cpp"]
    require(len(mains) == 1, "链接必须保留真实main对象，不能替换入口")
    return objects, generated_hashes, mains[0]


def collect_record(release_id):
    prepared = read_json(HERE / "prepared_inputs.json")
    require(prepared.get("schema") == SCHEMA and prepared.get("release_id") == release_id,
            "尚未绑定最终07发布；请先由总协调冻结最终输入")
    integration = integration_path(prepared["integration_directory"])
    identity, snapshot, rows, source_program = verify_stage(integration, release_id)
    require(identity == prepared["formal_identity"], "最终07构建或发布输入已经变化")
    require(read_json(HERE / "source_snapshot_07.json") == snapshot, "冻结清单不等于最终07清单")
    require(sha(HERE / "source_snapshot_07.json") == prepared["frozen_snapshot_sha256"], "冻结清单字节已变化")
    frozen = HERE / "frozen_07/sn100_host"
    verify_frozen(frozen, snapshot)
    require(sha(Path(prepared["source_archive"])) == prepared["source_archive_sha256"], "官方源码归档已变化")
    archive_parent = Path(prepared["source_archive"]).parent
    require(sha(archive_parent / "provenance.json") == prepared["archive_provenance_sha256"]
            and sha(archive_parent / "SHA256SUMS") == prepared["archive_manifest_sha256"],
            "官方源码归档的来源或校验说明已变化")
    templates = {name: sha(HERE / name) for name in TEMPLATE_NAMES}
    require(templates == prepared["template_sha256"], "已冻结的工具、检查器或runner发生变化")
    contracts = {name: sha(HERE / name) for name in CONTRACT_NAMES}
    require(contracts == prepared["contract_sha256"], "最终关闭条件发生变化")
    contract = read_json(HERE / "shutdown_contract.json")
    require(contract.get("release_id") == release_id and contract.get("main_sha256") == snapshot["shell/main.cpp"],
            "关闭条件未绑定最终main")
    n_guard = contract.get('n_shutdown_guard', {})
    n_sources = n_guard.get('source_sha256', {})
    require(isinstance(n_sources, dict) and set(n_sources) == {
        'shell/reagent_level_shell.cpp', 'shell/frmmain.cpp',
        'reagents/original_reagent_level_worker.cpp', 'reagents/original_reagent_level_worker.h'},
        'N生命周期没有绑定最终保护和worker源码')
    require(all(snapshot.get(name) == digest and sha(frozen / name) == digest
                for name, digest in n_sources.items()), 'N关门条件与最终冻结源码不符')
    require(n_guard.get('required_ready_marker') in contract['required_markers']
            and n_guard.get('n_function_verified') is False
            and n_guard.get('in_flight_n_close_verified') is False, 'N退出条件扩大成未经验证的业务结论')
    objects, generated, main_object = object_binding(integration, frozen, rows, source_program)
    record = {
        "schema": SCHEMA, "review_generation": "07", "release_id": release_id,
        "formal_identity": identity, "source_program": str(source_program), "source_count": len(snapshot),
        "frozen_directory": str(frozen), "frozen_snapshot_sha256": sha(HERE / "source_snapshot_07.json"),
        "prepared_inputs_sha256": sha(HERE / "prepared_inputs.json"),
        "source_archive_sha256": prepared["source_archive_sha256"],
        "template_sha256": templates, "contract_sha256": contracts,
        "object_count": len(objects), "objects": objects, "generated_sha256": generated,
        "main_object_kept": True, "main_object": main_object,
    }
    record["record_sha256"] = hashlib.sha256(canonical_json(record)).hexdigest()
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("release_id")
    parser.add_argument("--verify-objects", action="store_true", help="链接后重核全记录，不能改写原记录")
    args = parser.parse_args()
    release_id = validate_release_id(args.release_id)
    prepared = read_json(HERE / "prepared_inputs.json")
    integration = integration_path(prepared["integration_directory"])
    with build_lock(integration):
        record = collect_record(release_id)
        manifest = HERE / "link_provenance.json"
        object_text = "".join(item["path"] + "\n" for item in record["objects"])
        if args.verify_objects:
            require(read_json(manifest) == record, "链接前后完整输入记录不一致，不能发布检查器")
            require(regular(HERE / "link_objects.txt").read_text() == object_text, "链接对象清单发生变化")
        else:
            require(not manifest.exists() and not (HERE / "link_objects.txt").exists(), "链接证据已存在，不能覆盖")
            new_json(manifest, record)
            new_text(HERE / "link_objects.txt", object_text)
        print("REVIEW_BOUND_TO_07=" + release_id + "; SOURCE_FILES=" + str(record["source_count"])
              + "; OBJECTS=" + str(record["object_count"]) + "; RECORD_SHA256=" + record["record_sha256"])


if __name__ == "__main__":
    main()
