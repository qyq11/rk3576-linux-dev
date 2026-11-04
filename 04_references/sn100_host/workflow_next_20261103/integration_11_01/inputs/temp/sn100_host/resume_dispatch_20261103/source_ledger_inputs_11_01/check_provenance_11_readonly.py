"""只读检查草案来源覆盖与真实cwd；不导入或执行ledger。"""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json

OUT = Path(__file__).resolve().parent
ROOT = OUT.parents[3]


def rec(path):
    data = path.read_bytes()
    return {"path": path.relative_to(ROOT).as_posix(), "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)}


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def check_record(value):
    actual = rec(ROOT / value["path"])
    assert actual["sha256"] == value["sha256"]
    assert "bytes" not in value or value["bytes"] == actual["bytes"]
    return actual


def records(value):
    if isinstance(value, dict):
        if "path" in value and "sha256" in value:
            yield value
        else:
            for item in value.values():
                yield from records(item)
    elif isinstance(value, list):
        for item in value:
            yield from records(item)


def save(name, value):
    path = OUT / name
    assert not path.exists()
    path.write_bytes((json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8"))
    return rec(path)


source = OUT / "root_spec_11.draft.json"
spec = read(source)
assert spec["frozen"] is False and spec["all_intended_source_slices_present"] is False and spec["root_approval"] is None
assert spec["ledger_script"]["sha256"] == "307674360d5c24c3fe17eae04e5c7a869f7f35021fdaabecb9fd0afedbc165cd"
declared = {r["path"]: r for r in records(spec)}
for r in declared.values():
    assert rec(ROOT / r["path"]) == r
canonical = [OUT / n for n in ("mg04_check.canonical.json", "mg04_apply.canonical.json", "mg04_acceptance.canonical.json",
    "lis_profile_source_retry_01_check.canonical.json", "lis_profile_source_retry_01_apply.canonical.json",
    "lis_profile_source_acceptance.canonical.json", "root_reported_entry_results.pending_confirmation.json",
    "path_normalization_readonly_validation.json", "c7_baseline_archive_readonly_validation.json")]
for path in canonical:
    for r in records(read(path)):
        assert check_record(r) and r["path"] in declared, r
cwd_rows = []
for patch in spec["patches"]:
    for phase in ("check", "apply"):
        command = read(ROOT / patch[phase]["result"]["path"])
        expected_suffix = "\\06_programs\\sn100_host" if patch["name"] == "lis_profile_source" else "\\20260907_RK3576_Linux"
        assert command["cwd"].endswith(expected_suffix)
        assert command["phase"] == phase and command["actual_exit"] == 0
        assert command["argv"][:4] == ["git", "-c", "core.autocrlf=false", "apply"]
        assert ("--check" in command["argv"]) == (phase == "check")
        cwd_rows.append({"name": patch["name"], "phase": phase, "cwd": command["cwd"],
                         "result": patch[phase]["result"]})
spec["git_apply_config"]["explicit_actual_cwd_per_step"] = cwd_rows
spec["previous_unfrozen_draft"] = rec(source)
support = {r["path"]: r for r in spec["patches"][0]["supporting_records"]}
for path in (source, OUT / "preflight_11_readonly.json", OUT / "input_inventory_11.json", Path(__file__),
             OUT / "preparation_validation_attempt_01_failure.json"):
    support[rec(path)["path"]] = rec(path)
spec["patches"][0]["supporting_records"] = sorted(support.values(), key=lambda r: r["path"])
all_records = {r["path"]: r for r in records(spec)}
for r in all_records.values():
    assert rec(ROOT / r["path"]) == r
draft = save("root_spec_11.draft_with_coverage.json", spec)
save("provenance_11_readonly_validation.json", {"format": "sn100-integration11-provenance-coverage-readonly-v1",
    "prepared_at": datetime.now(timezone.utc).isoformat(), "draft": draft, "verified_record_paths": len(all_records),
    "derived_json_sources_explicitly_covered": True, "all_record_bytes_match": True, "actual_cwd_per_step": cwd_rows,
    "ledger_executed": False, "promotion_terminal_written": False, "frozen": False,
    "notice": "仅准备输入；全部strict门槛仍交原v2未来运行核对。本准备未执行其函数或生成终态。"})
print(json.dumps({"draft": draft, "record_paths_verified": len(all_records), "frozen": False, "ledger_executed": False}))
