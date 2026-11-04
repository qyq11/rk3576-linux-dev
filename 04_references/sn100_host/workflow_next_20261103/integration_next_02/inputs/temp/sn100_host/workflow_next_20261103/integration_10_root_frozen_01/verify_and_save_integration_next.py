"""下一批真实构建、暂存和恢复包均核准后，新建证据目录；不运行产品。"""
import os
import re
import shutil

from archive_integration_next import verify_package, verify_source_archive
from integration_next_tool_support import (
    ARCHIVE, CONFIG, EVIDENCE, PROGRAM, ROOT, WORK, cfg, check_record,
    checksums, local_path, now, ordinary_path, promotion, read_json, record,
    require_regular, safe_name, sha, source_snapshot, verify_build,
    verify_checksums, verify_sources, verify_stage, write_new,
)


def identity(item):
    return item["path"], item["sha256"]


def referenced_records(value):
    """只取元数据明确列出的文件记录，不递归复制目录。"""
    if isinstance(value, dict):
        if isinstance(value.get("path"), str) and isinstance(value.get("sha256"), str):
            yield value
        else:
            for child in value.values():
                yield from referenced_records(child)
    elif isinstance(value, list):
        for child in value:
            yield from referenced_records(child)


def historical_records(config, review):
    history = review["historical_failed_checks"]
    if not isinstance(history, list):
        raise RuntimeError("最终元数据须明确给出检查历史清单，可为空数组。")
    history_ids, evidence_ids = {identity(item) for item in history}, {identity(item) for item in config["evidence"]}
    if len(history_ids) != len(history):
        raise RuntimeError("前期检查历史原件重复。")
    for item in history:
        check_record(item)
        if identity(item) not in evidence_ids:
            raise RuntimeError("最终元数据的前期失败原件不在冻结证据清单。")
    return history


def object_inventory(complete):
    """核实际对象SHA并附其已重建验证的来源绑定，不复制对象文件。"""
    binding_path = check_record(complete["object_binding"])
    if binding_path != WORK / "object_binding.json":
        raise RuntimeError("对象来源绑定不在本批固定位置。")
    binding = read_json(binding_path)
    bound_objects = binding["objects"]
    if not isinstance(bound_objects, list):
        raise RuntimeError("对象绑定没有明确对象列表。")
    by_path = {}
    for entry in bound_objects:
        object_path = check_record(entry["object"])
        if (entry["object"] != record(object_path) or entry["object"]["path"] in by_path
                or entry["source_kind"] not in {"source", "moc", "resource", "helper"}):
            raise RuntimeError("对象绑定重复或实际对象记录不完整。")
        check_record(entry["source"])
        by_path[entry["object"]["path"]] = entry
    main_sources, helper_sources = set(), set()
    for line in require_regular(WORK / "build_inputs.tsv").read_text(encoding="utf-8").splitlines():
        kind, value = line.split("\t", 1)
        if kind in ("source", "helper_source"):
            path = local_path(value)
            require_regular(path, within=PROGRAM)
            (main_sources if kind == "source" else helper_sources).add(path)
    wanted = complete["objects"]
    expected_main = {"obj_" + str(index) + ".o" for index in range(wanted["main"])}
    expected_helper = {"helper_" + path.stem + ".o" for path in helper_sources - main_sources}
    if len(expected_helper) != wanted["helper"] or len(expected_main) != wanted["main"]:
        raise RuntimeError("对象声明与本批实际构建输入不一致。")
    actual = {path.name: path for path in (WORK / "arm64_build").glob("*.o")}
    if set(actual) != expected_main | expected_helper or len(actual) != wanted["total"]:
        raise RuntimeError("实际对象集合不等于本批构建终态。")
    if set(by_path) != {path.relative_to(ROOT).as_posix() for path in actual.values()}:
        raise RuntimeError("实际对象集合与来源绑定对象集合不相等。")
    entries = []
    for name, path in sorted(actual.items()):
        require_regular(path, within=WORK / "arm64_build")
        with path.open("rb") as source:
            header = source.read(20)
        if (len(header) != 20 or header[:6] != b"\x7fELF\x02\x01"
                or int.from_bytes(header[16:18], "little") != 1
                or int.from_bytes(header[18:20], "little") != 183):
            raise RuntimeError("实际对象不是ARM64 ELF64可重定位文件：" + name)
        object_record = record(path)
        entry = by_path[object_record["path"]]
        if entry["object"] != object_record:
            raise RuntimeError("实际对象SHA或字节数不等于已核来源绑定：" + name)
        entries.append({"path": path.relative_to(ROOT).as_posix(), "bytes": path.stat().st_size,
                        "sha256": object_record["sha256"], "object_binding_entry": entry,
                        "elf_class": 2, "machine": 183, "type": 1,
                        "role": "main" if name in expected_main else "helper"})
    return {"main": len(expected_main), "helper": len(expected_helper), "total": len(entries),
            "binding_record": record(binding_path), "files": entries}


def copy_new(source, target, expected):
    require_regular(source)
    ordinary_path(target, within=EVIDENCE)
    if sha(source) != expected:
        raise RuntimeError("复制前原证据已变化：" + str(source))
    target.parent.mkdir(parents=True, exist_ok=True)
    with source.open("rb") as original, target.open("xb") as output:
        shutil.copyfileobj(original, output, 1024 * 1024)
        output.flush()
        os.fsync(output.fileno())
    if sha(source) != expected or sha(target) != expected:
        raise RuntimeError("证据复制后字节不一致：" + str(target))


def write_text_new(path, text):
    ordinary_path(path, within=EVIDENCE)
    with path.open("x", encoding="utf-8", newline="\n") as output:
        output.write(text)
        output.flush()
        os.fsync(output.fileno())


def main():
    config = cfg()
    progress = promotion()
    snapshot = source_snapshot()
    verify_sources(snapshot)
    build, stage = verify_build(), verify_stage()
    binding_path = check_record(build["object_binding"])
    if binding_path != WORK / "object_binding.json":
        raise RuntimeError("构建终态引用了另一位置的对象绑定。")
    binding_record = record(binding_path)
    if stage["current_before"] != config["expected_current"] or stage["current_after"] != config["expected_current"]:
        raise RuntimeError("实际暂存的current不等于根冻结的本批原值。")
    review = config["review_metadata"]
    failures = historical_records(config, review)
    archive_record = record(WORK / "archive_complete.json")
    archived = read_json(WORK / "archive_complete.json")
    archive_count = verify_checksums(ARCHIVE)
    provenance = read_json(ARCHIVE / "provenance.json")
    if (require_regular(ARCHIVE / "object_binding.json").read_bytes() != binding_path.read_bytes()
            or check_record(provenance["object_binding"]) != binding_path
            or provenance["object_binding_copy_sha256"] != sha(ARCHIVE / "object_binding.json")
            or check_record(archived["object_binding_record"]) != binding_path):
        raise RuntimeError("正式归档对象绑定不等于本批真实构建原件。")
    complete_copy = read_json(ARCHIVE / "build_complete.json")
    if (archived.get("format") != "sn100-integration-next-archive-complete-v1" or archived.get("complete") is not True
            or archived["archive_path"] != ARCHIVE.relative_to(ROOT).as_posix()
            or archived["build_complete_sha256"] != sha(WORK / "build_complete.json")
            or archived["provenance_sha256"] != sha(ARCHIVE / "provenance.json")
            or archived["archive_checksums_sha256"] != sha(ARCHIVE / "SHA256SUMS")
            or complete_copy != build or sha(ARCHIVE / "build_complete.json") != sha(WORK / "build_complete.json")
            or local_path(require_regular(WORK / "archive_path.txt").read_text(encoding="utf-8").strip()).resolve() != ARCHIVE.resolve()):
        raise RuntimeError("实际恢复包完成记录、归档位置或逐份清单不完整。")
    package_name = safe_name(build["package"])
    match = re.fullmatch(r"sn100_shell_([a-f0-9]{64})\.tar\.gz", package_name)
    if not match:
        raise RuntimeError("本批包名称不是固定发布格式。")
    release = match.group(1)
    package = ARCHIVE / package_name
    source_archive = ARCHIVE / ("sn100_sources_" + release + ".tar.gz")
    if (archived["release_id"] != release or stage["release_id"] != release or provenance["release_id"] != release
            or sha(package) != build["package_sha256"] or sha(package) != provenance["package_sha256"]
            or sha(source_archive) != provenance["source_archive_sha256"]
            or provenance["stage_complete"] != stage
            or provenance["stage_complete_record"] != record(WORK / "stage_complete.json")
            or provenance["current_release_at_staging"] != stage["current_after"]
            or provenance["application_sha256"] != build["binaries"]["sn100_shell"]
            or provenance["report_renderer_sha256"] != build["binaries"]["sn100_report_renderer"]
            or provenance["source_files"] != len(snapshot) or provenance["patch_count"] != len(config["patches"])):
        raise RuntimeError("程序包、真实暂存、源码或最终补丁身份不一致。")
    package_files = verify_package(package, snapshot, build)
    source_count = verify_source_archive(source_archive, snapshot)
    if (source_count != len(snapshot) or archived["source_archive_readback_files"] != source_count
            or archived["package_archive_readback_files"] != len(package_files)
            or provenance["source_archive_readback_files"] != source_count
            or provenance["package_archive_readback_files"] != len(package_files)):
        raise RuntimeError("程序/源码包逐份回读数量与完成记录不符。")
    for name, source in (
        ("source_snapshot.json", WORK / "source_snapshot.json"),
        ("input_verification.json", WORK / "input_verification.json"),
        ("dependency_report.json", WORK / "dependencies/dependency_report.json"),
    ):
        if require_regular(ARCHIVE / name).read_bytes() != require_regular(source).read_bytes():
            raise RuntimeError("恢复包内实际构建依据与原件不等：" + name)
    objects = object_inventory(build)
    plan, by_target = [], {}
    excluded_old_archive = config["baseline_archive"]["path"]
    staged_package_path = (WORK / "packages" / package_name).relative_to(ROOT).as_posix()

    def add(source, relative, expected=None):
        source = require_regular(source)
        relative = safe_name(relative)
        if (source.suffix.lower() in {".db", ".sqlite", ".sqlite3", ".ini", ".pem", ".key", ".p12", ".pfx"}
                or ".ssh" in source.parts or "sysroot" in source.parts
                or source.relative_to(ROOT).as_posix() == excluded_old_archive):
            raise RuntimeError("本轮证据不收运行数据库、INI或认证原件：" + str(source))
        value = sha(source)
        if expected is not None and value != expected:
            raise RuntimeError("冻结证据指纹不符：" + str(source))
        if relative in by_target:
            if by_target[relative] != (source, value):
                raise RuntimeError("不同原件使用同一证据位置：" + relative)
            return
        by_target[relative] = (source, value)
        plan.append((source, relative, value))

    def add_record(item):
        source = check_record(item)
        if item["path"] in {excluded_old_archive, staged_package_path}:
            return  # 压缩包只核引用指纹；本批完整程序已另留正式恢复包，不重复复制。
        add(source, "inputs/" + source.relative_to(ROOT).as_posix(), item["sha256"])

    roots = {
        "build_complete.json": WORK / "build_complete.json", "input_verification.json": WORK / "input_verification.json",
        "build_inputs.tsv": WORK / "build_inputs.tsv", "source_snapshot.json": WORK / "source_snapshot.json",
        "merge_inputs.json": WORK / "merge_inputs.json", "build_exit.txt": WORK / "build_exit.txt",
        "build.log": WORK / "logs/build.log", "dependency_report.json": WORK / "dependencies/dependency_report.json",
        "stage_started.json": WORK / "stage_started.json", "stage_complete.json": WORK / "stage_complete.json",
        "stage_upload.sh": WORK / "stage_upload.sh", "upload.log": WORK / "logs/upload.log",
        "upload.exit": WORK / "upload.exit", "board_b_stage.log": WORK / "logs/board_b_stage.log",
        "board_b_stage.exit": WORK / "board_b_stage.exit", "archive_complete.json": WORK / "archive_complete.json",
        "live_guard_before.txt": WORK / "live_guard_before.txt", "live_guard_after.txt": WORK / "live_guard_after.txt",
        "archive_path.txt": WORK / "archive_path.txt", "provenance.json": ARCHIVE / "provenance.json",
        "object_binding.json": binding_path,
        "archive_README.md": ARCHIVE / "README.md", "archive_SHA256SUMS": ARCHIVE / "SHA256SUMS",
        "integration_next_tools_config.json": CONFIG,
        "promotion_progress.json": check_record(config["promotion_progress"]),
        "final_merge_review.json": check_record(config["final_review"]),
    }
    for relative, source in roots.items():
        add(source, relative)
    for source in WORK.glob("build*.json"):
        add(source, source.name)
    for item in [config["final_review"], config["promotion_progress"], config["promotion_script"],
                 config["final_source_manifest"], config["baseline_snapshot"],
                 build["object_binding"],
                 *config["patches"], *config["evidence"], *config["tools"], *failures]:
        add_record(item)
    for item in referenced_records(stage):
        add_record(item)
    for item in referenced_records(review):
        check_record(item)
        if item["path"] != excluded_old_archive:
            add_record(item)
    # patch记录用patch而不是path命名；其前后检查点由上面的递归记录收集。
    for item in review["patches"]:
        add_record({"path": item["patch"], "sha256": item["sha256"]})
    baseline = read_json(check_record(config["baseline_snapshot"]))
    changed = sorted(name for name in snapshot.keys() & baseline.keys() if snapshot[name] != baseline[name])
    added, deleted = sorted(snapshot.keys() - baseline.keys()), sorted(baseline.keys() - snapshot.keys())
    if (provenance["changed_files"] != len(changed) or provenance["added_files"] != len(added)
            or provenance["deleted_files"] != len(deleted)):
        raise RuntimeError("归档差异数量不是本批固定底稿和实际最终源码的差异。")
    for source, _, expected in plan:
        require_regular(source)
        if sha(source) != expected:
            raise RuntimeError("创建证据目录前原件已变化。")
    ordinary_path(EVIDENCE)
    output = WORK / "collection_complete.json"
    ordinary_path(output)
    if EVIDENCE.exists() or EVIDENCE.is_symlink() or output.exists() or output.is_symlink():
        raise RuntimeError("已有下一批证据或收集完成记录，不覆盖或自动重跑。")
    if (cfg() != config or promotion() != progress or source_snapshot() != snapshot
            or verify_build() != build or verify_stage() != stage
            or record(WORK / "archive_complete.json") != archive_record):
        raise RuntimeError("创建证据目录前本批输入、阶段终态或恢复包已变化。")
    verify_sources(snapshot)
    EVIDENCE.mkdir(parents=True, exist_ok=False)
    for source, relative, expected in plan:
        copy_new(source, EVIDENCE / relative, expected)
    write_new(EVIDENCE / "object_inventory.json", objects)
    verification = {
        "format": "sn100-integration-next-verification-v1", "captured_at": now(), "batch": "integration_next_02",
        "release_id": release, "arm64_build_exit": 0, "board_upload_exit": 0, "board_stage_exit": 0,
        "board": "board_b", "serial": "8314b1e8ffd0321f", "current_release_at_staging": stage["current_after"],
        "source_count": len(snapshot), "unique_build_inputs": build["unique_build_inputs"],
        "compiled_objects": objects["main"], "helper_objects": objects["helper"], "total_objects": objects["total"],
        "patch_count": len(config["patches"]), "changed_files": len(changed), "added_files": len(added), "deleted_files": len(deleted),
        "source_archive_readback_files": source_count, "package_archive_readback_files": len(package_files),
        "archive_checksum_files": archive_count, "copied_evidence_files": len(plan),
        "config_record": record(CONFIG), "review_record": record(check_record(config["final_review"])),
        "promotion_record": record(check_record(config["promotion_progress"])),
        "build_complete_record": record(WORK / "build_complete.json"), "stage_complete_record": record(WORK / "stage_complete.json"),
        "archive_complete_record": archive_record, "historical_failed_checks": failures,
        "object_binding_record": binding_record,
        "current_release_changed": False, "new_program_executed": False, "controller_restarted": False,
        "computer_product_executed": False, "rk_function_verified": False, "instrument_verified": False,
        "runtime_data_or_credentials_archived": False,
    }
    write_new(EVIDENCE / "verification.json", verification)
    write_text_new(EVIDENCE / "README.md", (
        "# 下一批构建、板B暂存和归档证据\n\n"
        f"版本`{release}`，补丁{len(config['patches'])}片、最终源码{len(snapshot)}项、实际构建输入{build['unique_build_inputs']}项。"
        f"实际主程序对象{objects['main']}个、辅助程序对象{objects['helper']}个；两类对象均读取文件头核为ARM64。\n\n"
        "完整交叉编译、打包、板B上传与独立目录暂存实际退出0后才建立本份证据；"
        "没有切换current、运行本版或重启主控。"
        f"暂存时current仍为`{stage['current_after']}`。源码、程序包的全部成员已只读回核，数量见verification.json。\n\n"
        "实际对象SHA和对应来源绑定保存为object_binding.json，归档、构建原件与证据副本保持同一原文。\n\n"
        "前期检查历史按本批最终元数据的完整显式清单保留；清单为空时不伪造先验失败。"
        "不臆造原件字段。inputs/按根相对路径保存明确引用的原件；"
        "根级build/stage文件同时保留，来源相同也不覆盖另一位置。provenance.json是正式恢复包原文。\n\n"
        "这里只证明构建和文件一致性。RK界面、业务、资源、真实仪器与长期运行仍待分别验证；"
        "不能用文件暂存或空闲启动结果代替实际业务与异常恢复验证。"
        "不含业务数据库、INI、认证原件、整个工具链或旧批源码压缩包，不自动提交Git。\n"
    ))
    count = checksums(EVIDENCE)
    if verify_checksums(EVIDENCE) != count:
        raise RuntimeError("新证据逐份清单数量不一致。")
    if (verify_checksums(ARCHIVE) != archive_count
            or sha(ARCHIVE / "SHA256SUMS") != archived["archive_checksums_sha256"]
            or record(WORK / "archive_complete.json") != archive_record):
        raise RuntimeError("收集期间正式恢复包或其终态变化。")
    if (verify_build() != build or record(binding_path) != binding_record
            or require_regular(EVIDENCE / "object_binding.json").read_bytes() != binding_path.read_bytes()
            or require_regular(ARCHIVE / "object_binding.json").read_bytes() != binding_path.read_bytes()):
        raise RuntimeError("收集结束时对象绑定及三处原文不一致。")
    write_new(output, {
        "format": "sn100-integration-next-collection-complete-v1", "complete": True, "completed_at": now(),
        "release_id": release, "evidence_path": EVIDENCE.relative_to(ROOT).as_posix(),
        "evidence_checksum_files": count, "evidence_checksums_sha256": sha(EVIDENCE / "SHA256SUMS"),
        "archive_complete_record": archive_record, "stage_complete_record": record(WORK / "stage_complete.json"),
        "verification_record": record(EVIDENCE / "verification.json"), "config_record": record(CONFIG),
        "object_binding_record": binding_record,
    })
    print("INTEGRATION_next_EVIDENCE_FILES=" + str(count))
    print("INTEGRATION_next_COLLECTION_COMPLETE=1\nPROGRAM_EXECUTED=0\nBOARD_CONNECTED_BY_COLLECTOR=0")


if __name__ == "__main__":
    main()
