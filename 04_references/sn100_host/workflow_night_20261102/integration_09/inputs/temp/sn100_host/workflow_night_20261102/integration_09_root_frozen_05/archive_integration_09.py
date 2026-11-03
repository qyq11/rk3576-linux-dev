"""板B暂存实际成功后，新建第09批程序和匹配源码归档，不包含运行资料。"""
from datetime import datetime
from pathlib import PurePosixPath
import hashlib
import io
import json
import os
import re
import shutil
import tarfile

from integration_09_tool_support import (
    ARCHIVE, CONFIG, PROGRAM, ROOT, WORK, cfg, ordinary_path, promotion,
    read_json, record, require_regular, sha, source_snapshot, verify_build, verify_stage, write_new,
)


PREFIX = "06_programs/sn100_host/"


def safe_name(name):
    if (not isinstance(name, str) or not name or "\\" in name or ":" in name
            or name.startswith("/") or any(part in ("", ".", "..") for part in name.split("/"))):
        raise RuntimeError("归档相对路径不安全：" + str(name))
    return PurePosixPath(name).as_posix()


def checked_record(item):
    path = ROOT / safe_name(item["path"])
    require_regular(path, within=ROOT)
    if sha(path) != item["sha256"]:
        raise RuntimeError("固定输入指纹已变化：" + item["path"])
    return path


def digest_stream(stream):
    value = hashlib.sha256()
    for block in iter(lambda: stream.read(1024 * 1024), b""):
        value.update(block)
    return value.hexdigest()


def verify_package(package, snapshot, complete):
    """只读包成员，不把程序解到电脑或执行其中脚本。"""
    require_regular(package, within=ROOT)
    matched = re.fullmatch(r"sn100_shell_([a-f0-9]{64})\.tar\.gz", package.name)
    if not matched:
        raise RuntimeError("程序包名称不是固定发布格式。")
    release = matched.group(1)
    files, special, seen = {}, {}, set()
    with tarfile.open(package, "r:gz") as bundle:
        for member in bundle:
            name = safe_name(member.name)
            if name in seen:
                raise RuntimeError("程序包成员重复：" + name)
            seen.add(name)
            if member.isdir():
                continue
            if not member.isfile():
                raise RuntimeError("程序包含链接或特殊成员：" + name)
            with bundle.extractfile(member) as stream:
                if name in ("SHA256SUMS", "release_id", "release.json"):
                    data = stream.read()
                    special[name] = data
                    files[name] = hashlib.sha256(data).hexdigest()
                else:
                    files[name] = digest_stream(stream)
    if (not {"SHA256SUMS", "release_id", "release.json"}.issubset(special)
            or hashlib.sha256(special["SHA256SUMS"]).hexdigest() != release
            or special["release_id"].decode("ascii").strip() != release):
        raise RuntimeError("程序包发布编号或清单指纹不符。")
    wanted_files = {}
    for line in special["SHA256SUMS"].decode("ascii").splitlines():
        wanted, name = line.split("  ", 1)
        name = safe_name(name)
        if not re.fullmatch(r"[a-f0-9]{64}", wanted) or name in wanted_files:
            raise RuntimeError("程序包清单重复或指纹格式不符。")
        wanted_files[name] = wanted
    actual = {name: value for name, value in files.items() if name not in ("SHA256SUMS", "release_id")}
    metadata = json.loads(special["release.json"])
    if (actual != wanted_files or metadata["sources"] != snapshot
            or metadata["binary_sha256"] != complete["binaries"]["sn100_shell"]
            or metadata["renderer_sha256"] != complete["binaries"]["sn100_report_renderer"]
            or metadata["dependencies_sha256"] != complete["dependency_report_sha256"]
            or files.get("sn100_shell") != complete["binaries"]["sn100_shell"]
            or files.get("sn100_report_renderer") != complete["binaries"]["sn100_report_renderer"]):
        raise RuntimeError("程序包实际文件、源码或依赖与本批构建不等。")
    return files


def copy_new(source, target, expected):
    require_regular(source, within=ROOT)
    if sha(source) != expected:
        raise RuntimeError("复制前输入已变化：" + str(source))
    target.parent.mkdir(parents=True, exist_ok=True)
    with source.open("rb") as original, target.open("xb") as output:
        shutil.copyfileobj(original, output, 1024 * 1024)
        output.flush()
        os.fsync(output.fileno())
    if sha(source) != expected or sha(target) != expected:
        raise RuntimeError("复制期间输入或目标发生变化：" + str(target))


def write_text_new(path, text, encoding="utf-8"):
    with path.open("x", encoding=encoding, newline="\n") as output:
        output.write(text)
        output.flush()
        os.fsync(output.fileno())


def verify_source_archive(path, snapshot):
    require_regular(path, within=ROOT)
    seen = set()
    with tarfile.open(path, "r:gz") as bundle:
        for member in bundle:
            if not member.isfile() or not member.name.startswith(PREFIX):
                raise RuntimeError("源码包包含非普通源码成员。")
            name = safe_name(member.name[len(PREFIX):])
            if name in seen or name not in snapshot:
                raise RuntimeError("源码包成员重复或不在固定清单：" + name)
            with bundle.extractfile(member) as stream:
                if digest_stream(stream) != snapshot[name]:
                    raise RuntimeError("源码包回读指纹不符：" + name)
            seen.add(name)
    if seen != set(snapshot):
        raise RuntimeError("源码包没有完整覆盖本批固定清单。")
    return len(seen)


def main():
    config = cfg()  # 未给最终清单和指纹时在这里拒绝，不产生归档目录。
    expected_current = config["expected_current"]
    if not isinstance(expected_current, str) or not re.fullmatch(r"releases/[a-f0-9]{64}", expected_current):
        raise RuntimeError("根尚未冻结本批实际current，不猜测或沿用旧批次。")
    verified_build = verify_build()
    staged = verify_stage()  # 没有本批真实暂存0，不建立正式恢复归档。
    stage_record = record(WORK / "stage_complete.json")
    snapshot = source_snapshot()
    progress = promotion()
    fixed_progress = checked_record(config["promotion_progress"])
    fixed_review = checked_record(config["final_review"])
    fixed_manifest = checked_record(config["final_source_manifest"])
    fixed_base_archive = checked_record(config["baseline_archive"])
    fixed_base_snapshot = checked_record(config["baseline_snapshot"])
    baseline = read_json(fixed_base_snapshot)
    patch_records = config["patches"]
    if not patch_records:
        raise RuntimeError("最终补丁清单为空，不归档。")
    for item in [*patch_records, *config["evidence"]]:
        checked_record(item)
    complete_path = WORK / "build_complete.json"
    inputs_path = WORK / "input_verification.json"
    complete, inputs = read_json(complete_path), read_json(inputs_path)
    binding_path = checked_record(complete["object_binding"])
    if binding_path != WORK / "object_binding.json" or complete != verified_build:
        raise RuntimeError("对象绑定不是本批构建终态指定的原件。")
    binding_record = record(binding_path)
    required_inputs = (
        WORK / "build_exit.txt", WORK / "logs/build.log", WORK / "source_snapshot.json",
        WORK / "merge_inputs.json", complete_path, inputs_path,
        WORK / "dependencies/dependency_report.json",
    )
    for path in required_inputs:
        require_regular(path, within=WORK)
    if ((WORK / "build_exit.txt").read_text().strip() != "0"
            or "ARM64_INTEGRATION_09_BUILD_AND_PACKAGE_PASSED=1" not in (WORK / "logs/build.log").read_text(encoding="utf-8").splitlines()
            or complete.get("format") != "sn100-integration-09-build-complete-v1"
            or complete.get("arm64_build_and_package_passed") is not True
            or complete["promotion_progress_sha256"] != sha(fixed_progress)
            or (WORK / "merge_inputs.json").read_bytes() != fixed_progress.read_bytes()
            or read_json(WORK / "source_snapshot.json") != snapshot
            or complete["source_snapshot_sha256"] != sha(WORK / "source_snapshot.json")
            or complete["source_count"] != len(snapshot)
            or inputs["source_count"] != len(snapshot)
            or inputs["unique_build_inputs"] != complete["unique_build_inputs"]
            or inputs["kinds"] != complete["kinds"]):
        raise RuntimeError("本批真实晋级、完整构建和打包尚未完成或证据不等。")
    if (read_json(fixed_review)["final_source_files"] != snapshot or read_json(fixed_manifest) != snapshot
            or progress["final_source_files"] != snapshot):
        raise RuntimeError("构建清单不等于最终审查及晋级清单。")
    dependency_path = WORK / "dependencies/dependency_report.json"
    dependencies = read_json(dependency_path)
    if (sha(dependency_path) != complete["dependency_report_sha256"]
            or dependencies.get("offline_link_and_sql_closure_passed") is not True):
        raise RuntimeError("实际ARM64静态依赖核对未通过。")
    if set(complete["binaries"]) != {"sn100_shell", "sn100_report_renderer"}:
        raise RuntimeError("缺少本批两个程序的明确指纹。")
    for name, wanted in complete["binaries"].items():
        path = WORK / "build" / name
        require_regular(path, within=WORK)
        if sha(path) != wanted:
            raise RuntimeError("本批真实程序指纹不符：" + name)
    package_name = safe_name(complete["package"])
    matched = re.fullmatch(r"sn100_shell_([a-f0-9]{64})\.tar\.gz", package_name)
    if not matched:
        raise RuntimeError("程序包名称不是本批固定发布格式。")
    release = matched.group(1)
    if (staged["release_id"] != release or staged["package_sha256"] != complete["package_sha256"]
            or staged["current_before"] != expected_current or staged["current_after"] != expected_current):
        raise RuntimeError("实际暂存与本批程序包或原current不一致。")
    package = WORK / "packages" / package_name
    require_regular(package, within=WORK / "packages")
    if sha(package) != complete["package_sha256"]:
        raise RuntimeError("真实构建程序包指纹不符。")
    package_files = len(verify_package(package, snapshot, complete))
    for name, wanted in snapshot.items():
        path = PROGRAM / safe_name(name)
        require_regular(path, within=PROGRAM)
        if sha(path) != wanted:
            raise RuntimeError("正式源码已变化，未生成混合版本归档：" + name)
    outputs = (ARCHIVE, WORK / "archive_path.txt", WORK / "archive_complete.json")
    for path in outputs:
        ordinary_path(path, within=ROOT)
    if any(path.exists() or path.is_symlink() for path in outputs):
        raise RuntimeError("已有第09批归档或完成记录，不覆盖、不自动重跑。")
    before_sources = {name: value for name, value in baseline.items()}
    changed = sorted(name for name in snapshot.keys() & before_sources.keys() if snapshot[name] != before_sources[name])
    added = sorted(snapshot.keys() - before_sources.keys())
    deleted = sorted(before_sources.keys() - snapshot.keys())
    if verify_stage() != staged or record(WORK / "stage_complete.json") != stage_record:
        raise RuntimeError("板B暂存终态在归档前已变化。")
    ARCHIVE.mkdir(parents=True, exist_ok=False)
    source_archive = ARCHIVE / ("sn100_sources_" + release + ".tar.gz")
    with tarfile.open(source_archive, "x:gz") as bundle:
        for name, wanted in sorted(snapshot.items()):
            content = (PROGRAM / name).read_bytes()
            if hashlib.sha256(content).hexdigest() != wanted:
                raise RuntimeError("归档期间正式源码变化：" + name)
            member = tarfile.TarInfo(PREFIX + name)
            member.size, member.mode = len(content), 0o644
            bundle.addfile(member, io.BytesIO(content))
    source_files = verify_source_archive(source_archive, snapshot)
    target_package = ARCHIVE / package_name
    copy_new(package, target_package, complete["package_sha256"])
    if len(verify_package(target_package, snapshot, complete)) != package_files:
        raise RuntimeError("归档程序包完整回读数量不符。")
    copied = {
        WORK / "source_snapshot.json": "source_snapshot.json", complete_path: "build_complete.json",
        inputs_path: "input_verification.json", dependency_path: "dependency_report.json",
        binding_path: "object_binding.json",
    }
    for source, name in copied.items():
        copy_new(source, ARCHIVE / name, sha(source))
    if (verify_build() != verified_build or record(binding_path) != binding_record
            or require_regular(ARCHIVE / "object_binding.json").read_bytes() != binding_path.read_bytes()):
        raise RuntimeError("复制后构建对象、绑定或其归档原文变化。")
    provenance = {
        "format": "sn100-integration-09-provenance-v1", "created_at": datetime.now().astimezone().isoformat(),
        "batch": "integration_09", "release_id": release, "intended_board": "board_b", "serial": "8314b1e8ffd0321f",
        "source_files": len(snapshot), "unique_build_inputs": inputs["unique_build_inputs"],
        "patch_count": len(patch_records), "changed_files": len(changed), "added_files": len(added), "deleted_files": len(deleted),
        "changed_paths": changed, "added_paths": added, "deleted_paths": deleted,
        "application_sha256": complete["binaries"]["sn100_shell"], "report_renderer_sha256": complete["binaries"]["sn100_report_renderer"],
        "package_sha256": sha(target_package), "source_archive_sha256": sha(source_archive),
        "source_snapshot_sha256": complete["source_snapshot_sha256"], "promotion_progress_sha256": sha(fixed_progress),
        "object_binding": dict(complete["object_binding"]), "object_binding_copy_sha256": sha(ARCHIVE / "object_binding.json"),
        "final_review": record(fixed_review), "final_source_manifest": record(fixed_manifest),
        "baseline_archive": record(fixed_base_archive), "baseline_snapshot": record(fixed_base_snapshot),
        "promotion_progress": record(fixed_progress), "tools_config": record(CONFIG),
        "source_archive_readback_files": source_files, "package_archive_readback_files": package_files,
        "build_and_dependencies": "passed", "board_staging": "passed_manifest_sha256_and_runtime_dependencies",
        "stage_complete": staged, "stage_complete_record": stage_record,
        "current_release_at_staging": staged["current_after"], "board_activation": "not_performed",
        "rk_function": "pending_rk_validation", "instrument": "not_verified", "runtime_data_included": False,
    }
    write_new(ARCHIVE / "provenance.json", provenance)
    write_text_new(ARCHIVE / "README.md", (
        "# 板B第09批程序和匹配源码\n\n"
        f"版本`{release}`。本批共{len(patch_records)}片补丁，源码{len(snapshot)}项、实际构建输入{inputs['unique_build_inputs']}项；"
        f"相对固定底稿修改{len(changed)}项、新增{len(added)}项、删除{len(deleted)}项。\n\n"
        "这份归档只说明ARM64完整编译、静态依赖检查、打包和板B独立版本目录的实际文件暂存已通过。"
        "暂存实际退出0，硬件序列号为`8314b1e8ffd0321f`；没有切换current、启动本版或重启主控。"
        f"暂存时current仍为`{staged['current_after']}`。RK界面、业务、资源、真实仪器与长期运行仍待分别验证，"
        "不能用文件归档代替实际业务和异常恢复验证。\n\n"
        f"源码包逐份回读{source_files}项，程序包逐份回读{package_files}项，均与本批实际构建相同。"
        "指纹、最终审查及晋级来源见`provenance.json`、`source_snapshot.json`和`SHA256SUMS`。"
        "实际对象SHA及其来源绑定原文另见`object_binding.json`，副本与本批构建原件逐字节一致。"
        "本批前期资料检查历史按最终元数据保留；历史清单为空时不套用旧批次的失败前提。\n\n"
        "恢复范围仅本批程序、匹配源码和静态依赖说明；不包含INI、患者、账户、业务数据库、工具链或整机镜像。"
        "恢复前先确认板卡身份和现有环境；程序解到独立版本目录后核清单与动态库，源码解到独立目录后逐份核指纹，"
        "不要覆盖后来的修改。两个压缩包须由Git LFS管理，不自动提交Git。\n"
    ))
    rows = [sha(path) + "  " + path.name for path in sorted(ARCHIVE.iterdir()) if path.is_file()]
    write_text_new(ARCHIVE / "SHA256SUMS", "\n".join(rows) + "\n", "ascii")
    for row in rows:
        wanted, name = row.split("  ", 1)
        if sha(ARCHIVE / name) != wanted:
            raise RuntimeError("最终归档清单逐份回读不等：" + name)
    if (verify_build() != verified_build or record(binding_path) != binding_record
            or require_regular(ARCHIVE / "object_binding.json").read_bytes() != binding_path.read_bytes()):
        raise RuntimeError("归档收尾时本批对象绑定或原文发生变化。")
    write_text_new(WORK / "archive_path.txt", str(ARCHIVE) + "\n")
    write_new(WORK / "archive_complete.json", {
        "format": "sn100-integration-09-archive-complete-v1", "complete": True, "release_id": release,
        "archive_path": ARCHIVE.relative_to(ROOT).as_posix(), "source_archive_readback_files": source_files,
        "package_archive_readback_files": package_files, "provenance_sha256": sha(ARCHIVE / "provenance.json"),
        "archive_checksums_sha256": sha(ARCHIVE / "SHA256SUMS"), "build_complete_sha256": sha(complete_path),
        "object_binding_record": binding_record,
    })
    print("ARCHIVE=" + str(ARCHIVE))
    print("SOURCE_ARCHIVE_READBACK_FILES=" + str(source_files))
    print("PACKAGE_ARCHIVE_READBACK_FILES=" + str(package_files))
    print("ARCHIVE_INTEGRATION_09_COMPLETE=1\nPROGRAM_EXECUTED=0\nBOARD_CONNECTED=0")


if __name__ == "__main__":
    main()
