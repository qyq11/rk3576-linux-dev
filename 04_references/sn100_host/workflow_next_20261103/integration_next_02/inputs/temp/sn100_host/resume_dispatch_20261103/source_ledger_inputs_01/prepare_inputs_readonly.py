"""只读核对本轮已保存证据，另写未冻结草案；不调用来源聚合器，不运行Git或产品。"""
from pathlib import Path, PurePosixPath
from datetime import datetime, timezone
import ast
import hashlib
import json
import tarfile

OUT = Path(__file__).resolve().parent
ROOT = OUT.parents[3]
RESUME = ROOT / "temp/sn100_host/resume_dispatch_20261103"
CHAIN = ROOT / "temp/sn100_host/workflow_next_20261103/integration_10_source_acceptance_01"
OLD = ROOT / "07_backups/sn100_host/board_b/workflow_integration_20261102/integration_09"
USER = ROOT / "temp/sn100_host/workflow_night_20261102/user_task_origin_init_revision_04_20261103_01"
LIS = ROOT / "temp/sn100_host/lis_profile_queue_candidate_20261103_01"
CAL = ROOT / "temp/task_reagent_cal_metadata_candidate_01"
WHEN = datetime.now(timezone.utc).isoformat()
READ = {}


def sha(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def rec(path):
    path = path.resolve()
    path.relative_to(ROOT)
    assert path.is_file() and not path.is_symlink(), path
    item = {"path": path.relative_to(ROOT).as_posix(), "sha256": sha(path), "bytes": path.stat().st_size}
    old = READ.setdefault(item["path"], item)
    assert old == item, path
    return item


def read(path):
    rec(path)
    return json.loads(path.read_text(encoding="utf-8-sig"))


def save(name, value):
    path = OUT / name
    assert not path.exists(), path
    path.write_bytes((json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8"))
    return rec(path)


def changed(before, after):
    return sorted(n for n in before.keys() | after.keys() if before.get(n) != after.get(n))


def safe_source_map(path):
    value = read(path)
    assert isinstance(value, dict) and value
    for name, digest in value.items():
        assert not PurePosixPath(name).is_absolute() and ".." not in PurePosixPath(name).parts
        assert len(digest) == 64 and all(c in "0123456789abcdef" for c in digest)
    return value


def old_archive_check():
    archive = OLD / "sn100_sources_b31f581fd240e09ee4c0a4a4eb6c4cbdd674fa9cf8897fcd170e9d0ca058d993.tar.gz"
    baseline = OLD / "source_snapshot.json"
    b066 = ROOT / "temp/sn100_host/workflow_night_20261102/integration_09_revision_03_review/source_manifest_after.json"
    expected = safe_source_map(baseline)
    comparison = safe_source_map(b066)
    members = {}
    with tarfile.open(archive, "r|gz") as stream:
        for member in stream:
            assert member.isfile() and not member.issym() and not member.islnk(), member.name
            prefix = "06_programs/sn100_host/"
            assert member.name.startswith(prefix), member.name
            name = member.name[len(prefix):]
            assert name not in members and name in expected and ".." not in PurePosixPath(name).parts
            h = hashlib.sha256()
            source = stream.extractfile(member)
            assert source is not None
            with source:
                for block in iter(lambda: source.read(1024 * 1024), b""):
                    h.update(block)
            members[name] = h.hexdigest()
    assert members == expected == comparison
    provenance = read(OLD / "provenance.json")
    sums = OLD / "SHA256SUMS"
    rec(sums)
    archive_digest = sha(archive)
    assert archive_digest == provenance["source_archive_sha256"]
    assert (archive_digest + "  " + archive.name) in sums.read_text(encoding="utf-8-sig").splitlines()
    members_record = save("baseline_archive_member_sha256.json", members)
    result = {
        "format": "sn100-readonly-archive-byte-check-v1", "prepared_at": WHEN,
        "archive": rec(archive), "archive_provenance": rec(OLD / "provenance.json"),
        "archive_sha256sums": rec(sums), "archive_sha_matches_old_provenance_and_sums": True,
        "baseline_snapshot": rec(baseline), "b066_comparison": rec(b066),
        "member_digest_map": members_record, "regular_members": len(members),
        "streamed_without_extract": True, "archive_members_equal_baseline": True,
        "baseline_equals_b066_dictionary": True,
        "root_confirmation_required": True,
        "meaning": "仅核旧源包字节与旧684表一致；不是本次构建、运行或实物验收结果。",
    }
    save("baseline_archive_readonly_check.json", result)
    return baseline, archive, expected


def correction(name, approved_root):
    entry = CHAIN / name
    folder = entry / "line_ending_correction_01"
    failed = safe_source_map(entry / "source_after.json")
    expected = safe_source_map(entry / "expected_after.json")
    final = safe_source_map(folder / "source_after.json")
    first_path = folder / "first_attempt_result.json"
    inspection_path = (folder if name == "user_init_04" else entry) / "postimage_mismatch_inspection.json"
    first, inspection = read(first_path), read(inspection_path)
    acceptance = read(entry / "acceptance.json")
    paths = changed(failed, expected)
    assert paths == sorted(acceptance["line_ending_correction"])
    assert final == expected and changed(failed, final) == paths
    assert len(paths) == (10 if name == "user_init_04" else 2)
    assert first["actual_exit"] == 1 and first["git_check_actual_exit"] == 0 and first["git_apply_actual_exit"] == 0
    assert {row["path"] for row in inspection} == set(paths)
    backups, approved, canonical_rows, proof_rows = [], [], [], []
    for row in inspection:
        p = row["path"]
        backup = folder / "before" / p
        target = approved_root / p
        b, a = backup.read_bytes(), target.read_bytes()
        br, ar = rec(backup), rec(target)
        assert br["sha256"] == failed[p] and ar["sha256"] == expected[p]
        assert b != a and b.replace(b"\r\n", b"\n") == a.replace(b"\r\n", b"\n")
        raw_actual = row.get("actual", row.get("actual_sha256"))
        raw_expected = row.get("expected", row.get("approved_sha256"))
        assert raw_actual == failed[p] and raw_expected == expected[p] and row["equal_after_crlf_to_lf"] is True
        backups.append({"relative_file": p, "record": br})
        approved.append({"relative_file": p, "record": ar})
        proof_rows.append({"path": p, "failed_backup": br, "approved_postimage": ar,
                           "backup_equals_failed_map": True, "approved_equals_expected_map": True,
                           "equal_after_crlf_to_lf": True})
        canonical_rows.append({**row, "actual": failed[p], "expected": expected[p],
            "current_equals_failed_snapshot": True,
            "derived_field_notice": "此固定兼容键指归档补正前备份与失败表匹配；不是当前正式源码仍为失败后像。",
            "comparison_subject": "archived_failed_before_bytes_not_current_formal",
            "original_inspection": rec(inspection_path), "failed_backup": br, "approved_postimage": ar})
    validation = save(name + "_line_ending_readonly_validation.json", {
        "format": "sn100-derived-line-ending-proof-v1", "prepared_at": WHEN, "derived": True,
        "failed_checkpoint": rec(entry / "source_after.json"), "expected_after": rec(entry / "expected_after.json"),
        "final_after": rec(folder / "source_after.json"), "original_first_result": rec(first_path),
        "original_inspection": rec(inspection_path), "original_acceptance": rec(entry / "acceptance.json"),
        "paths": paths, "rows": proof_rows, "failed_to_final_only_crlf_lf": True,
        "final_equals_expected": True, "root_confirmation_required": True,
        "meaning": "本次仅独立读取原备份及批准字节核对；未重跑补正入口或修改任何原件。",
    })
    canonical_first, canonical_inspection = rec(first_path), rec(inspection_path)
    if name == "user_init_04":
        assert first["original_logs_and_failed_source_map_preserved"] is True
        canonical_first = save("user_init_04_first_result.canonical.json", {
            **first, "format": "sn100-derived-first-attempt-result-v1", "derived": True,
            "mismatch_paths": paths, "original_comparison_and_logs_preserved": True,
            "original_first_result": rec(first_path), "readonly_validation": validation,
            "mapping_notice": "新增键是事后按原失败表、原日志与归档备份核实的映射；原件当时没有这些键。",
        })
        canonical_inspection = save("user_init_04_inspection.canonical.json", canonical_rows)
    else:
        assert first["original_comparison_and_logs_preserved"] is True and sorted(first["mismatch_paths"]) == paths
        assert all(row["current_equals_failed_snapshot"] is True for row in inspection)
    return {"kind": "exact-crlf-postimage-v1", "paths": paths,
            "failed_checkpoint": rec(entry / "source_after.json"),
            "first_entry_result": canonical_first, "inspection": canonical_inspection,
            "script": rec(RESUME / ("finish_user_exact_postimage_and_lis.py" if name == "user_init_04" else "finish_cal_exact_postimage.py")),
            "backups": sorted(backups, key=lambda v: v["relative_file"]),
            "approved_postimages": sorted(approved, key=lambda v: v["relative_file"]),
            "original_first_result": rec(first_path), "original_inspection": rec(inspection_path),
            "original_acceptance": rec(entry / "acceptance.json"), "readonly_validation": validation}


def current_inventory():
    script = ROOT / "06_programs/sn100_host/shell/package_arm64.py"
    tree = ast.parse(script.read_bytes())
    function = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == "snapshot")
    tuples = sorted((n for n in ast.walk(function) if isinstance(n, ast.Tuple)), key=lambda n: (n.lineno, n.col_offset))
    assert len(tuples) == 4
    directories, excluded, files, protocol_files = [ast.literal_eval(n) for n in tuples]
    assert excluded == (".md", ".pyc")
    program = ROOT / "06_programs/sn100_host"
    paths = set()
    for directory in directories:
        paths.update(p for p in (program / directory).rglob("*") if p.is_file() and p.suffix not in excluded and "__pycache__" not in p.parts)
    paths.update(program / n for n in files)
    paths.update(program / "protocol" / n for n in protocol_files)
    result = {}
    for path in sorted(paths):
        assert not path.is_symlink()
        result[path.relative_to(program).as_posix()] = sha(path)
    return result


def main():
    original_ledger = rec(RESUME / "source_ledger_candidate.py")
    original_contract = rec(RESUME / "source_ledger_contract.md")
    assert original_ledger["sha256"] == "738fa974c4fb74e5de82485d423bf91995571f9b0022570330d6d15286118ac7"
    ledger_v2 = rec(OUT / "source_ledger_candidate_v2.py")
    baseline, archive, previous = old_archive_check()
    cal_correction = correction("cal", CAL / "candidate/06_programs/sn100_host")
    user_correction = correction("user_init_04", USER / "candidate/06_programs/sn100_host")
    tool_attestation = save("root_reported_user_correction_tool_result.pending_confirmation.json", {
        "format": "sn100-root-reported-tool-result-retrospective-v1", "prepared_at": WHEN,
        "derived": True, "reported_by": "root", "recorded_by": "review_dispatch_dependencies",
        "reported_tool_result": {"tool": "exec_command", "tool_result_block": "07c46d", "actual_exit": 0},
        "script": rec(RESUME / "finish_user_exact_postimage_and_lis.py"),
        "root_reported_stdout_summaries": [
            {"phase": "user_init_04", "source_map_sha256": rec(CHAIN / "user_init_04/line_ending_correction_01/source_after.json")["sha256"]},
            {"phase": "lis_profile_queue", "source_map_sha256": rec(CHAIN / "lis_profile_queue/source_after.json")["sha256"]}],
        "basis": "Root在本轮委派消息中明确提供真实Tool块07c46d实际exit0及两条stdout摘要。此文件由子Agent事后记录，待Root在冻结批准中认可。",
        "rerun_by_preparer": False, "original_stdout_log_created": False,
        "root_confirmation_required": True,
    })
    user_original_acceptance = read(CHAIN / "user_init_04/acceptance.json")
    user_acceptance = save("user_init_04_acceptance.canonical.json", {
        **user_original_acceptance, "derived": True, "correction_actual_exit": 0,
        "original_acceptance": rec(CHAIN / "user_init_04/acceptance.json"),
        "correction_exit_basis": tool_attestation,
        "readonly_validation": user_correction["readonly_validation"],
        "mapping_notice": "correction_actual_exit是事后按Root真实Tool结果补记，原acceptance仅有byte_correction_completed；未修改原件。",
        "root_confirmation_required": True,
    })
    observer_original = read(CHAIN / "observer_04/acceptance.json")
    observer_exit_path = RESUME / "observer_install_04.exit"
    observer_exit = int(observer_exit_path.read_text(encoding="utf-8-sig").strip())
    assert observer_exit == 0 and observer_original["installer_actual_exit"] is None
    observer_after = safe_source_map(CHAIN / "observer_04/source_after.json")
    assert len(observer_after) == observer_original["source_count"]
    assert observer_after == safe_source_map(CHAIN / "observer_04/expected_after.json")
    assert observer_original["source_map_sha256"] == sha(CHAIN / "observer_04/source_after.json")
    observer_acceptance = save("observer_04_acceptance.canonical.json", {
        **observer_original, "derived": True, "source_files": len(observer_after), "installer_actual_exit": observer_exit,
        "original_acceptance": rec(CHAIN / "observer_04/acceptance.json"),
        "outer_runner": {"exit": rec(observer_exit_path), "stdout": rec(RESUME / "observer_install_04.stdout.log"),
                         "stderr": rec(RESUME / "observer_install_04.stderr.log")},
        "mapping_notice": "source_files由原source_count及实际after/expected表共同核实；installer_actual_exit来自既有外层.exit，原acceptance null原样保留。",
        "root_confirmation_required": True,
    })
    definitions = [
        ("ascii", ROOT / "temp/sn100_host/communication_ascii_candidate_20261103_02_r1/communication_ascii_A2_r1.patch",
         [RESUME / "ascii_a2_r1_independent_review.md"], [RESUME / "accept_ascii_source.py"]),
        ("cal", CAL / "packaging_01/task_reagent_cal_metadata_onb066.patch",
         [RESUME / n for n in ("cal_candidate_resume_review.md", "cal_high_risk_independent_review.md", "cal_saved_hole_us_followup.md")],
         [RESUME / n for n in ("accept_cal_source.py", "inspect_cal_postimage.py", "finish_cal_exact_postimage.py")]),
        ("user_init_04", USER / "packaging_01/user_task_origin_shared_initialization_on7cad.patch",
         [RESUME / "user_init_04_configuration_independent_review.md", RESUME / "user_init_04_lifecycle_independent_review.md",
          USER / "reviews/origin_initialization_review_01.md", USER / "reviews/lifecycle_initialization_review_01.md", USER / "handoff.md"],
         [RESUME / "accept_user_and_lis_sources.py", RESUME / "finish_user_exact_postimage_and_lis.py"]),
        ("lis_profile_queue", LIS / "lis_profile_queue.patch",
         [RESUME / "lis_profile_queue_independent_review.md", LIS / "source_review.md", LIS / "handoff.md"],
         [RESUME / "accept_user_and_lis_sources.py", RESUME / "finish_user_exact_postimage_and_lis.py"]),
        ("observer_04", CHAIN / "observer_04/observer_04.patch",
         [CHAIN / "observer_04/reviews" / n for n in ("lis_keep_running_03_targeted_review.md", "lis_keep_running_04_targeted_review.md", "lis_keep_running_independent_review.md")],
         [RESUME / "install_lis_observation_sources_candidate.py"]),
        ("ascii_b1", RESUME / "ascii_b1_root_merge_01/ascii_b1_on_observer_04.patch",
         [RESUME / n for n in ("ascii_b1_business_independent_review.md", "ascii_b1_lifecycle_independent_review.md", "ascii_b1_merged_business_review.md", "ascii_b1_merged_lifecycle_review.md")],
         [RESUME / "accept_ascii_b1_merged.py"]),
    ]
    patches, history, argv_policy, summary = [], [], {}, []
    for index, (name, patch, reviews, scripts) in enumerate(definitions, 1):
        folder = CHAIN / name
        before_path = folder / "source_before.json"
        after_path = folder / "source_after.json"
        corrected = cal_correction if name == "cal" else user_correction if name == "user_init_04" else None
        if corrected is not None:
            after_path = folder / "line_ending_correction_01/source_after.json"
        before, after, expected = safe_source_map(before_path), safe_source_map(after_path), safe_source_map(folder / "expected_after.json")
        assert before == previous and after == expected and before != after
        patch_record = rec(patch)
        item = {"index": index, "name": name, "patch": patch_record,
                "before_checkpoint": rec(before_path), "after_checkpoint": rec(after_path),
                "expected_after": rec(folder / "expected_after.json"), "scope": changed(before, after),
                "reviews": [rec(p) for p in reviews], "adoption_scripts": [rec(p) for p in scripts],
                "acceptance": user_acceptance if name == "user_init_04" else observer_acceptance if name == "observer_04" else rec(folder / "acceptance.json"),
                "correction": corrected,
                "original_acceptance": rec(folder / "acceptance.json")}
        supporting = [rec(folder / "acceptance.json")]
        if corrected is not None:
            supporting.extend([corrected["original_first_result"], corrected["original_inspection"],
                               corrected["readonly_validation"]])
        if name == "user_init_04":
            supporting.append(tool_attestation)
        if name == "observer_04":
            supporting.extend([rec(observer_exit_path), rec(RESUME / "observer_install_04.stdout.log"),
                               rec(RESUME / "observer_install_04.stderr.log")])
            for mapping in observer_original["tool_mapping"]:
                source = ROOT / mapping["source"].replace("\\", "/")
                destination = ROOT / mapping["destination"].replace("\\", "/")
                sr, dr = rec(source), rec(destination)
                assert sr["sha256"] == dr["sha256"] == mapping["sha256"]
                supporting.extend([sr, dr])
            for mapping in observer_original["new_source_mapping"]:
                source = ROOT / mapping["source"].replace("\\", "/")
                sr = rec(source)
                assert sr["sha256"] == mapping["sha256"]
                supporting.append(sr)
        item["supporting_records"] = list({r["path"]: r for r in supporting}.values())
        for phase in ("check", "apply"):
            value = read(folder / (phase + "_result.json"))
            assert value["actual_exit"] == 0 and value["phase"] == phase and "apply" in value["argv"]
            assert ("--check" in value["argv"]) == (phase == "check")
            terminal = value["argv"][-1].replace("\\", "/")
            assert terminal.rsplit("/", 1)[-1] == patch.name
            item[phase] = {"result": rec(folder / (phase + "_result.json")),
                           "stdout": rec(folder / (phase + ".stdout.log")), "stderr": rec(folder / (phase + ".stderr.log"))}
        acceptance = read(ROOT / item["acceptance"]["path"])
        assert acceptance["actual_source_equals_expected"] is True and acceptance["source_files"] == len(after)
        if corrected is None:
            assert acceptance["patch_sha256"] == patch_record["sha256"]
        else:
            assert acceptance["correction_actual_exit"] == 0 and acceptance["source_map_sha256"] == sha(after_path)
            history.extend([rec(folder / "source_after.json"), rec(folder / "line_ending_correction_01/first_attempt_result.json"),
                            corrected["original_inspection"], corrected["readonly_validation"]])
        argv_policy[name] = {phase: {"result": item[phase]["result"], "argv": read(ROOT / item[phase]["result"]["path"])["argv"]} for phase in ("check", "apply")}
        patches.append(item)
        summary.append({"name": name, "before_files": len(before), "after_files": len(after), "scope_files": len(item["scope"]),
                        "after_sha256": sha(after_path), "check_actual_exit": 0, "apply_actual_exit": 0,
                        "line_ending_correction_files": len(corrected["paths"]) if corrected else 0})
        previous = after
    final_path = CHAIN / "ascii_b1/source_after.json"
    assert sha(final_path) == "f741feea28fcb6a2bcff8d6c2bda49faa67d3b72e3508062e5f1e368b2422b8b"
    actual = current_inventory()
    assert actual == previous
    attributes = rec(ROOT / ".gitattributes")
    assert attributes["sha256"] == "c46d8032dc7311b5911dfb7e578ad70ba1258ac924b72675f8b2192c5b9d445a"
    spec = {
        "format": "sn100-root-source-ledger-spec-v1", "frozen": False, "owner": "root",
        "all_intended_source_slices_present": False,
        "scope_notice": "当前这一批仅ASCII→CAL→User04→LIS3→Observer04→B1；未采用的五个纯字段codec不在本批。Root需核定范围后才置true。",
        "prepared_at": WHEN, "prepared_by": "review_dispatch_dependencies", "ledger_script": ledger_v2,
        "root_approval": None, "baseline_snapshot": rec(baseline), "baseline_archive": rec(archive),
        "final_source_manifest": rec(final_path), "snapshot_rules": rec(ROOT / "06_programs/sn100_host/shell/package_arm64.py"),
        "root_attributes": attributes,
        "git_apply_config": {"recorded_per_step_actual_argv": argv_policy, "root_readonly_config_capture": None,
                             "notice": "前三片实际命令原样保留；LIS3及后续实际使用命令级-c core.autocrlf=false。不声称所有阶段统一选项。Root仍须加入实际只读Git配置日志record。"},
        "required_patch_names": [p["name"] for p in patches], "patches": patches,
        "historical_failed_checks": history,
        "output_directory": "temp/sn100_host/resume_dispatch_20261103/root_source_ledger_final_01",
        "pending_root_actions": ["亲读并认可事后Tool事实及User固定兼容键的归档语义", "确认09基线归档及当前六片批范围",
                                 "独立审v2只增加supporting_records检查的最小差异，并确认来源记录均进入工具冻结证据",
                                 "加入实际只读Git配置日志record", "写真实Root批准文件并填root_approval record",
                                 "重新核当前全源和证据未漂移后另存冻结spec；不覆盖此草案"],
        "derived_records": {"user_correction_tool_result": tool_attestation,
                            "user_acceptance": user_acceptance, "observer_acceptance": observer_acceptance},
    }
    spec_record = save("root_spec.draft.json", spec)
    save("draft_preflight_readonly.json", {
        "format": "sn100-root-spec-draft-readonly-check-v1", "prepared_at": WHEN, "draft": spec_record,
        "available_evidence_checks_passed": True, "six_slice_chain": summary,
        "current_source_files": len(actual), "current_source_equals_last_actual_after": True,
        "ledger_candidate_executed": False, "git_executed": False, "build_executed": False,
        "product_executed": False, "board_operated": False, "promotion_terminal_written": False,
        "original_ledger": original_ledger, "original_contract": original_contract,
        "freeze_blockers": spec["pending_root_actions"],
    })
    assert rec(RESUME / "source_ledger_candidate.py") == original_ledger
    assert rec(RESUME / "source_ledger_contract.md") == original_contract
    for record in list(READ.values()):
        assert rec(ROOT / record["path"]) == record
    save("evidence_inventory.json", {"format": "sn100-readonly-input-inventory-v1", "prepared_at": WHEN,
        "notice": "只读准备时实际核过的输入和新派生记录；本清单不自包含其自身哈希。", "records": sorted(READ.values(), key=lambda r: r["path"])})
    print(json.dumps({"draft": spec_record, "source_files": len(actual), "slices": len(patches),
                      "frozen": False, "ledger_executed": False, "archive_stream_checked": True}, ensure_ascii=False))


if __name__ == "__main__":
    main()
