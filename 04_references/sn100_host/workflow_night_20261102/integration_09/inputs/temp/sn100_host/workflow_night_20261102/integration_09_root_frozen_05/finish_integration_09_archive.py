"""第09批最后只读核包、证据和LFS归属，再新建完成记录；不改已有归档。"""
import json
import subprocess
from integration_09_tool_support import (
    ROOT, WORK, ARCHIVE, EVIDENCE, cfg, promotion, read_json, sha, record,
    check_record, require_regular, verify_build, verify_stage, verify_checksums, write_new, now,
)


def same_record(item, expected_path):
    if check_record(item) != expected_path:
        raise RuntimeError("完成凭据引用了另一位置：" + str(expected_path))


def main():
    config = cfg()
    promotion()
    build = verify_build()
    stage = verify_stage()
    binding_path = WORK / "object_binding.json"
    same_record(build["object_binding"], binding_path)
    if stage["current_before"] != config["expected_current"] or stage["current_after"] != config["expected_current"]:
        raise RuntimeError("实际暂存的current不等于根冻结的本批原值。")
    target = WORK / "finish_complete.json"
    if target.exists() or target.is_symlink():
        raise RuntimeError("已有第09批收口记录，不覆盖或自动重跑。")
    complete = read_json(WORK / "collection_complete.json")
    if (complete.get("format") != "sn100-integration-09-collection-complete-v1"
            or complete.get("complete") is not True or complete.get("release_id") != build["release_id"]
            or complete.get("evidence_path") != EVIDENCE.relative_to(ROOT).as_posix()
            or complete.get("evidence_checksums_sha256") != sha(EVIDENCE / "SHA256SUMS")):
        raise RuntimeError("本批证据收集尚未完整结束，未生成最终记录。")
    same_record(complete["archive_complete_record"], WORK / "archive_complete.json")
    same_record(complete["stage_complete_record"], WORK / "stage_complete.json")
    same_record(complete["verification_record"], EVIDENCE / "verification.json")
    same_record(complete["object_binding_record"], binding_path)
    archive = read_json(WORK / "archive_complete.json")
    provenance = read_json(ARCHIVE / "provenance.json")
    verification = read_json(EVIDENCE / "verification.json")
    same_record(archive["object_binding_record"], binding_path)
    same_record(provenance["object_binding"], binding_path)
    same_record(verification["object_binding_record"], binding_path)
    if (require_regular(ARCHIVE / "object_binding.json").read_bytes() != require_regular(binding_path).read_bytes()
            or require_regular(EVIDENCE / "object_binding.json").read_bytes() != binding_path.read_bytes()
            or provenance["object_binding_copy_sha256"] != sha(ARCHIVE / "object_binding.json")):
        raise RuntimeError("归档、证据和构建对象绑定不是同一原文。")
    if (archive.get("format") != "sn100-integration-09-archive-complete-v1" or archive.get("complete") is not True
            or archive.get("release_id") != build["release_id"]
            or archive.get("archive_path") != ARCHIVE.relative_to(ROOT).as_posix()
            or archive.get("archive_checksums_sha256") != sha(ARCHIVE / "SHA256SUMS")
            or archive.get("provenance_sha256") != sha(ARCHIVE / "provenance.json")
            or archive.get("build_complete_sha256") != sha(WORK / "build_complete.json")
            or provenance.get("release_id") != build["release_id"]
            or provenance.get("stage_complete") != stage
            or verification.get("release_id") != build["release_id"]
            or verification.get("arm64_build_exit") != 0 or verification.get("board_stage_exit") != 0
            or verification.get("rk_function_verified") is not False or verification.get("instrument_verified") is not False):
        raise RuntimeError("归档、同板暂存和证据终态不一致。")
    if require_regular(EVIDENCE / "provenance.json").read_bytes() != require_regular(ARCHIVE / "provenance.json").read_bytes():
        raise RuntimeError("正式归档和证据来源说明不是同一原文。")
    # README和SHA256SUMS已由各自阶段一次生成；这里全部只读，不覆写清单。
    require_regular(ARCHIVE / "README.md")
    require_regular(EVIDENCE / "README.md")
    archive_files = verify_checksums(ARCHIVE)
    evidence_files = verify_checksums(EVIDENCE)
    packages = ["sn100_shell_" + build["release_id"] + ".tar.gz", "sn100_sources_" + build["release_id"] + ".tar.gz"]
    relative = [(ARCHIVE / name).relative_to(ROOT).as_posix() for name in packages]
    result = subprocess.run(["git", "check-attr", "filter", "diff", "merge", "--", *relative],
                            cwd=ROOT, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    for package in relative:
        for attribute in ("filter", "diff", "merge"):
            if result.stdout.splitlines().count(package + ": " + attribute + ": lfs") != 1:
                raise RuntimeError("正式恢复包未由Git LFS管理：" + package)
    # 查完属性后再核一次终态，避免以检查开始时的旧字节收口。
    if verify_build() != build or verify_stage() != stage or read_json(WORK / "collection_complete.json") != complete:
        raise RuntimeError("收口检查期间实际输入或终态发生变化。")
    if (require_regular(ARCHIVE / "object_binding.json").read_bytes() != binding_path.read_bytes()
            or require_regular(EVIDENCE / "object_binding.json").read_bytes() != binding_path.read_bytes()):
        raise RuntimeError("收口检查期间对象绑定副本变化。")
    if verify_checksums(ARCHIVE) != archive_files or verify_checksums(EVIDENCE) != evidence_files:
        raise RuntimeError("收口检查期间归档集合发生变化。")
    write_new(target, {
        "format": "sn100-integration-09-finish-complete-v1", "complete": True, "completed_at": now(),
        "release_id": build["release_id"], "collection_complete_record": record(WORK / "collection_complete.json"),
        "archive_complete_record": record(WORK / "archive_complete.json"),
        "object_binding_record": record(binding_path),
        "archive_checksums_record": record(ARCHIVE / "SHA256SUMS"), "evidence_checksums_record": record(EVIDENCE / "SHA256SUMS"),
        "archive_listed_files": archive_files, "evidence_listed_files": evidence_files,
        "git_lfs_attributes": result.stdout, "current_changed": False, "program_started": False,
        "rk_function_verified": False, "instrument_verified": False,
    })
    print(json.dumps({"finish_complete": True, "archive_listed_files": archive_files,
                      "evidence_listed_files": evidence_files}, ensure_ascii=False))
    print("INTEGRATION_09_ARCHIVE_CLOSURE=1\nPRODUCT_EXECUTED=0\nBOARD_CONNECTED=0")


if __name__ == "__main__":
    main()
