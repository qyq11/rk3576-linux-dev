"""只读核第11批原件并写未冻结草案；不运行ledger、Git、构建、产品或板卡。"""
from pathlib import Path, PurePosixPath
from datetime import datetime, timezone
import ast
import hashlib
import json
import tarfile

OUT = Path(__file__).resolve().parent
ROOT = OUT.parents[3]
RESUME = OUT.parent
ACCEPT = ROOT / "temp/sn100_host/workflow_next_20261103/integration_11_source_acceptance_02"
FIRST = ROOT / "temp/sn100_host/workflow_next_20261103/integration_11_source_acceptance_01"
COMBO = ROOT / "temp/sn100_host/workflow_next_20261103/root_combined_candidate_11_02"
ARCHIVE = ROOT / "07_backups/sn100_host/board_b/workflow_integration_20261103/integration_next_02"
F741 = ROOT / "temp/sn100_host/workflow_next_20261103/integration_10_source_acceptance_01/ascii_b1/source_after.json"
LEDGER = RESUME / "source_ledger_inputs_01/source_ledger_candidate_v2.py"
PROGRAM = ROOT / "06_programs/sn100_host"
NOW = datetime.now(timezone.utc).isoformat()
INPUTS = {}


def sha(path):
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1048576), b""):
            value.update(block)
    return value.hexdigest()


def rec(path):
    path = path.resolve()
    path.relative_to(ROOT)
    assert path.is_file() and not path.is_symlink(), path
    value = {"path": path.relative_to(ROOT).as_posix(), "sha256": sha(path), "bytes": path.stat().st_size}
    assert INPUTS.setdefault(value["path"], value) == value
    return value


def read(path):
    rec(path)
    return json.loads(path.read_text(encoding="utf-8-sig"))


def save(name, value):
    path = OUT / name
    assert not path.exists(), path
    path.write_bytes((json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8"))
    return rec(path)


def mapping(path):
    result = read(path)
    assert isinstance(result, dict) and result
    for name, digest in result.items():
        assert "\\" not in name and ":" not in name and not PurePosixPath(name).is_absolute()
        assert all(p not in ("", ".", "..") for p in name.split("/"))
        assert len(digest) == 64 and all(c in "0123456789abcdef" for c in digest)
    return result


def native_map(path):
    native = read(path)
    result = {name.replace("\\", "/"): digest for name, digest in native.items()}
    assert len(result) == len(native)
    return result


def delta(before, after):
    return sorted(n for n in before.keys() | after.keys() if before.get(n) != after.get(n))


def native_root():
    value = ROOT.as_posix()
    assert value.startswith("/mnt/c/")
    return ("C:/" + value[7:]).replace("/", "\\")


def normalized_capture(folder, stem):
    document = folder / (stem + ".path_normalization.json")
    value = read(document)
    raw = folder / (stem + "_native_raw.json")
    canonical = folder / (stem + ".json")
    assert value["native_raw_sha256"] == sha(raw) and value["canonical_sha256"] == sha(canonical)
    assert value["native_raw"] == rec(raw)["path"] and value["canonical"] == rec(canonical)["path"]
    assert value["source_bytes_changed"] is False
    assert native_map(raw) == mapping(canonical)
    assert len(mapping(canonical)) == value["file_count"]
    exit_path = folder / (stem + ".actual_exit")
    assert int(exit_path.read_text().strip()) == 0
    for suffix in (".actual_exit", ".stdout.log", ".stderr.log"):
        rec(folder / (stem + suffix))
    return {"normalization": rec(document), "native_raw": rec(raw), "canonical": rec(canonical),
            "snapshot_tool_actual_exit": rec(exit_path), "file_count": value["file_count"],
            "one_to_one_key_separator_mapping": True, "digest_values_unchanged": True}


def archive_baseline():
    source = ARCHIVE / "sn100_sources_c7cbf438a3a034b25ae22accfaf48bacf0717174ce85bef2203ce8db143cb06e.tar.gz"
    snapshot = ARCHIVE / "source_snapshot.json"
    baseline = mapping(snapshot)
    assert baseline == mapping(F741) and len(baseline) == 687
    provenance, sums = read(ARCHIVE / "provenance.json"), ARCHIVE / "SHA256SUMS"
    rec(sums)
    assert sha(source) == provenance["source_archive_sha256"] == "56fbff3871eec9776205487ef59d1c9826b6605ec4f8f884bc90b6ceede6b4bf"
    assert sha(snapshot) == provenance["source_snapshot_sha256"] == "fd50fedc204888ba6277029715caa80054bd3c760d2c80e1e9c9b93762abb4fc"
    assert (sha(source) + "  " + source.name) in sums.read_text(encoding="utf-8-sig").splitlines()
    members = {}
    with tarfile.open(source, "r|gz") as stream:
        for member in stream:
            prefix = "06_programs/sn100_host/"
            assert member.isfile() and not member.issym() and not member.islnk() and member.name.startswith(prefix)
            name = member.name[len(prefix):]
            assert name not in members and name in baseline
            value = hashlib.sha256()
            content = stream.extractfile(member)
            assert content is not None
            with content:
                for block in iter(lambda: content.read(1048576), b""):
                    value.update(block)
            members[name] = value.hexdigest()
    assert members == baseline
    member_record = save("c7_archive_member_sha256.json", members)
    proof = save("c7_baseline_archive_readonly_validation.json", {
        "format": "sn100-integration11-c7-baseline-readonly-v1", "prepared_at": NOW,
        "archive": rec(source), "snapshot": rec(snapshot), "f741_before": rec(F741),
        "provenance": rec(ARCHIVE / "provenance.json"), "sha256sums": rec(sums), "members": member_record,
        "regular_members": len(members), "streamed_without_extract": True,
        "member_bytes_equal_snapshot": True, "snapshot_dictionary_equals_f741": True,
        "root_confirmation_required": True,
        "notice": "c7发布号只是文件命名关联值；tar.gz使用真实56fb摘要。两份源码JSON排版不同，raw SHA保留。"})
    return snapshot, source, baseline, proof


def command(folder, phase, script, default_cwd):
    original = folder / (phase + ".command.json")
    value = read(original)
    exit_path = folder / (phase + ".actual_exit")
    actual_exit = int(exit_path.read_text().strip())
    assert actual_exit == value["actual_exit"] == 0
    assert value["argv"][:4] == ["git", "-c", "core.autocrlf=false", "apply"]
    assert ("--check" in value["argv"]) == (phase == "check")
    canonical = save(folder.name + "_" + phase + ".canonical.json", {
        **value, "format": "sn100-derived-actual-git-command-v1", "derived": True, "phase": phase,
        "cwd": value.get("cwd", default_cwd), "original_command": rec(original),
        "original_actual_exit": rec(exit_path), "execution_script": rec(script),
        "mapping_notice": "phase为本次事后字段映射；argv/退出来自原件。缺cwd时按实际Root脚本显式cwd=ROOT记录，未重跑命令。"})
    return {"result": canonical, "stdout": rec(folder / (phase + ".stdout.log")),
            "stderr": rec(folder / (phase + ".stderr.log"))}


def inventory():
    rules = PROGRAM / "shell/package_arm64.py"
    function = next(n for n in ast.parse(rules.read_bytes()).body if isinstance(n, ast.FunctionDef) and n.name == "snapshot")
    tuples = sorted((n for n in ast.walk(function) if isinstance(n, ast.Tuple)), key=lambda n: (n.lineno, n.col_offset))
    assert len(tuples) == 4
    directories, excluded, files, protocols = [ast.literal_eval(n) for n in tuples]
    assert excluded == (".md", ".pyc")
    paths = set()
    for directory in directories:
        paths.update(p for p in (PROGRAM / directory).rglob("*") if p.is_file() and p.suffix not in excluded and "__pycache__" not in p.parts)
    paths.update(PROGRAM / p for p in files)
    paths.update(PROGRAM / "protocol" / p for p in protocols)
    assert all(not p.is_symlink() for p in paths)
    rec(rules)
    return {p.relative_to(PROGRAM).as_posix(): sha(p) for p in sorted(paths)}


def main():
    assert rec(LEDGER)["sha256"] == "307674360d5c24c3fe17eae04e5c7a869f7f35021fdaabecb9fd0afedbc165cd"
    rec(Path(__file__))
    baseline_path, archive_path, baseline, baseline_proof = archive_baseline()
    normalizations = [normalized_capture(ACCEPT, "source_before"), normalized_capture(ACCEPT / "mg04", "source_after"),
                      normalized_capture(ACCEPT / "lis_profile_source_retry_01", "source_before"),
                      normalized_capture(ACCEPT / "lis_profile_source_retry_01", "source_after")]
    first_raw = read(FIRST / "source_before.json")
    assert native_map(FIRST / "source_before.json") == baseline and first_raw != baseline
    assert int((FIRST / "source_before.actual_exit").read_text().strip()) == 0
    assert sorted(p.name for p in FIRST.iterdir()) == sorted(["source_before.json", "source_before.actual_exit", "source_before.stderr.log", "source_before.stdout.log"])
    failed_lis = ACCEPT / "lis_profile_source"
    failed_check = read(failed_lis / "check.command.json")
    assert failed_check["actual_exit"] == int((failed_lis / "check.actual_exit").read_text().strip()) == 1
    assert not (failed_lis / "apply.actual_exit").exists() and not (failed_lis / "acceptance.json").exists()
    for folder in (FIRST, failed_lis):
        for path in folder.iterdir():
            if path.is_file():
                rec(path)
    root_report = save("root_reported_entry_results.pending_confirmation.json", {
        "format": "sn100-root-reported-integration11-tool-results-v1", "prepared_at": NOW, "derived": True,
        "reported_by": "root", "recorded_by": "review_dispatch_dependencies", "root_confirmation_required": True,
        "entries": [
            {"script": rec(RESUME / "accept_sources_11_01.py"), "actual_exit": None,
             "reported_fact": "native snapshot工具退出0，但键分隔符使与f741字典比较失败；在split/apply之前停止。Root未在委派消息提供外层退出数值，不预填。"},
            {"script": rec(RESUME / "accept_sources_11_02.py"), "actual_exit": 1,
             "reported_fact": "Mg分步0/0且后像一致，随后LIS在ROOT cwd检查1、未apply，导致整体入口1，不能写整体0。"},
            {"script": rec(RESUME / "resume_lis_profile_source_11_03.py"), "actual_exit": 0,
             "reported_fact": "Root委派消息提供真实Tool0；在PROGRAM cwd重试LIS0/0并核最终after。此文件是事后记录，不伪造原Tool stdout文件。"}],
        "preparer_reran_entries": False, "original_tool_stdout_fabricated": False})
    normalization_proof = save("path_normalization_readonly_validation.json", {
        "format": "sn100-integration11-path-normalization-readonly-v1", "prepared_at": NOW,
        "captures": normalizations, "first_failed_raw": rec(FIRST / "source_before.json"),
        "first_raw_normalized_equals_c7_and_f741": True, "native_key_dictionary_differs_from_f741": True,
        "source_byte_correction_executed": False,
        "notice": "仅把表键的反斜杠一对一映射为正斜杠，摘要值与源字节未改变；不是CRLF源码补正，也没有改旧表。"})
    names = ["combined_11_business_review_01.md", "combined_11_lifecycle_review_01.md",
             "combined_11_business_review_02.md", "combined_11_lifecycle_review_02.md",
             "reagent_probe_04_business_targeted_review_01.md", "reagent_probe_final_lifecycle_review_01.md",
             "reagent_probe_final_business_review_01.md", "reagent_probe_scope_independent_review.md",
             "lis_profile_source_independent_review_01.md"]
    reviews = [rec(RESUME / name) for name in names]
    mgroot = ROOT / "temp/sn100_host/reagent_single_probe_20261103_04"
    lisroot = ROOT / "temp/sn100_host/lis_profile_source_candidate_20261103_01"
    extra = [mgroot / name for name in ("handoff_04.md", "delivery_manifest_04.json", "freeze_04.json", "delta_03_to_04/single_probe_03_to_04.patch")]
    extra += [lisroot / name for name in ("handoff.md", "baseline.json", "source_review.md", "verification.json")]
    extra += [COMBO / name for name in ("sparse_after_17.json", "composed_expected_after_688.json", "guard_scope_revision_02.json", "packaging_01/mg04_lis_profile_on_f741.patch", "packaging_01/acceptance.json")]
    extra += [RESUME / name for name in ("prepare_sparse_combination_11_01.py", "finish_sparse_combination_11_01.py", "refine_combined_probe_guard_11_02.py", "package_sparse_combination_11_02.py")]
    for path in extra:
        rec(path)
    records = []
    previous = baseline
    definitions = [("mg04", ACCEPT / "mg04", ACCEPT / "mg04_on_f741_root_revision02.patch", RESUME / "accept_sources_11_02.py", ACCEPT / "mg04/expected_after.json"),
                   ("lis_profile_source", ACCEPT / "lis_profile_source_retry_01", lisroot / "lis_profile_source.patch", RESUME / "resume_lis_profile_source_11_03.py", failed_lis / "expected_after.json")]
    summaries = []
    for index, (name, folder, patch, script, expected_path) in enumerate(definitions, 1):
        before, after, expected = mapping(folder / "source_before.json"), mapping(folder / "source_after.json"), mapping(expected_path)
        original = read(folder / "acceptance.json")
        assert before == previous and after == expected and original["all_after_bytes_equal"] is True
        assert original["source_files"] == len(after) == 688 and original["source_after_sha256"] == sha(folder / "source_after.json")
        assert original["patch_sha256"] == sha(patch)
        scope = delta(before, after)
        assert scope == sorted(original["paths"]) and len(scope) == (15 if name == "mg04" else 2)
        for path in scope:
            assert sha(COMBO / "candidate/06_programs/sn100_host" / path) == after[path]
        canonical = save(name + "_acceptance.canonical.json", {
            **original, "derived": True, "actual_source_equals_expected": original["all_after_bytes_equal"],
            "original_acceptance": rec(folder / "acceptance.json"), "expected_after": rec(expected_path),
            "source_map_sha256": sha(folder / "source_after.json"),
            "mapping_notice": "只把原all_after_bytes_equal映射为ledger字段；逐步git0和前后表另核。没有将外层入口1改成0，也没有补真未来构建/板端。"})
        for original_review in original.get("reviews", []):
            assert rec(ROOT / original_review["path"])["sha256"] == original_review["sha256"]
        record = {"index": index, "name": name, "patch": rec(patch), "before_checkpoint": rec(folder / "source_before.json"),
                  "after_checkpoint": rec(folder / "source_after.json"), "expected_after": rec(expected_path), "scope": scope,
                  "reviews": reviews, "adoption_scripts": [rec(script)], "acceptance": canonical, "correction": None,
                  "check": command(folder, "check", script, native_root()), "apply": command(folder, "apply", script, native_root()),
                  "supporting_records": [rec(folder / "acceptance.json"), root_report, normalization_proof]}
        records.append(record)
        summaries.append({"name": name, "scope_files": len(scope), "before_files": len(before), "after_files": len(after),
                          "after_sha256": sha(folder / "source_after.json"), "check_actual_exit": 0, "apply_actual_exit": 0})
        previous = after
    assert sha(ACCEPT / "mg04/source_after.json") == "66ece18e56690e31d6b911775ccd8ae6fc70b982c04c430bf02d322b4029dbd5"
    final_path = ACCEPT / "lis_profile_source_retry_01/source_after.json"
    assert sha(final_path) == "b0788f899b63a982c55ae85d098d7f4e843bdbedbd19b9f7c1ca0d5a3c569767"
    assert previous == mapping(COMBO / "composed_expected_after_688.json")
    actual = inventory()
    assert actual == previous
    source_complete = read(ACCEPT / "source_complete.json")
    assert source_complete["scope"] == "source_acceptance_only" and source_complete["compiled"] is False and source_complete["board_operated"] is False
    rec(ACCEPT / "source_complete.json")
    attributes = rec(ROOT / ".gitattributes")
    assert attributes["sha256"] == "c46d8032dc7311b5911dfb7e578ad70ba1258ac924b72675f8b2192c5b9d445a"
    git_logs = [RESUME / ("git_apply_environment_20261103_01" + suffix) for suffix in (".stdout.log", ".stderr.log", ".exit")]
    assert int(git_logs[2].read_text().strip()) == 0
    for path in git_logs:
        rec(path)
    history = [rec(path) for folder in (FIRST, failed_lis) for path in sorted(folder.iterdir()) if path.is_file()]
    history.append(root_report)
    # 显式覆盖原件，不能只把来源藏在派生JSON文字里；v2逐项checked后进入required_cfg_evidence。
    records[0]["supporting_records"] = sorted(INPUTS.values(), key=lambda v: v["path"])
    spec = {"format": "sn100-root-source-ledger-spec-v1", "owner": "root", "frozen": False,
            "all_intended_source_slices_present": False, "root_approval": None, "prepared_at": NOW,
            "ledger_script": rec(LEDGER), "baseline_snapshot": rec(baseline_path), "baseline_archive": rec(archive_path),
            "snapshot_rules": rec(PROGRAM / "shell/package_arm64.py"), "root_attributes": attributes,
            "final_source_manifest": rec(final_path), "required_patch_names": [v[0] for v in definitions], "patches": records,
            "historical_failed_checks": history,
            "git_apply_config": {"historical_root_readonly_capture": [rec(p) for p in git_logs],
                "actual_per_step_commands": [{"name": p["name"], "check": p["check"]["result"], "apply": p["apply"]["result"]} for p in records],
                "root_stage11_confirmation_or_new_capture": None,
                "notice": "旧只读capture不是本准备重新执行；本批每步actual argv确有命令级core.autocrlf=false，Mg cwd ROOT、LIS重试cwd PROGRAM分别保留。"},
            "output_directory": "temp/sn100_host/resume_dispatch_20261103/root_source_ledger_final_11_01",
            "scope_notice": "本批只含Mg15 Root组合修订02与LIS来源两源；source_complete只指源码阶段。未来编译、发布、板端及业务结果不在此草案。",
            "pending_root_confirmation": ["确认c7真实源包/源表为本批基线，f741字典相等但raw SHA不同", "确认两片范围及原失败保留；首accept01外层退出数值未提供，不得假填",
                "亲读真实Tool结果并认可02整体1与03实际0的事后记录", "确认历史Git配置capture适用本批或另补新的实际只读记录",
                "独立审canonical字段映射及v2证据覆盖，写真实Root批准并另存冻结spec", "冻结前重核688当前源和所有输入未漂移"]}
    draft = save("root_spec_11.draft.json", spec)
    save("preflight_11_readonly.json", {"format": "sn100-integration11-input-preparation-readonly-v1", "prepared_at": NOW,
        "draft": draft, "slices": summaries, "source_files_at_preparation": len(actual), "current_source_equals_actual_final_after": True,
        "baseline_validation": baseline_proof, "path_normalization_validation": normalization_proof,
        "ledger_executed": False, "git_executed": False, "build_executed": False, "board_operated": False,
        "promotion_terminal_written": False, "root_scope_and_approval_pending": True})
    for record in list(INPUTS.values()):
        assert rec(ROOT / record["path"]) == record
    save("input_inventory_11.json", {"format": "sn100-readonly-integration11-input-inventory-v1", "prepared_at": NOW,
        "records": sorted(INPUTS.values(), key=lambda v: v["path"]), "notice": "本清单不自包含自身摘要；不是来源工具终态。"})
    print(json.dumps({"draft": draft, "source_files": len(actual), "slice_count": len(records), "frozen": False,
                      "ledger_executed": False, "archive_sha256": sha(archive_path)}, ensure_ascii=False))


if __name__ == "__main__":
    main()
