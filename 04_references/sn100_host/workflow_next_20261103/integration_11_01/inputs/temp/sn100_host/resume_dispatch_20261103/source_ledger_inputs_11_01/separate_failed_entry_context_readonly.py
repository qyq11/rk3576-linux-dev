"""只把未冻结草案的失败上下文与成功重试分列，不改任何原件或既有草案。"""
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


def save(name, value):
    path = OUT / name
    assert not path.exists()
    path.write_bytes((json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8"))
    return rec(path)


prior = OUT / "root_spec_11.draft_with_coverage.json"
spec = read(prior)
report_path = OUT / "root_reported_entry_results.pending_confirmation.json"
report = read(report_path)
assert [entry["actual_exit"] for entry in report["entries"]] == [None, 1, 0]
failed = save("root_reported_failed_entries.pending_confirmation.json", {
    **report, "format": "sn100-root-reported-integration11-failed-entry-context-v1",
    "derived_from": rec(report_path), "entries": report["entries"][:2],
    "notice": "只列首轮表比较失败和02整体1；成功03只留supporting_records，不列为失败入口。"})
spec["historical_failed_checks"] = [r for r in spec["historical_failed_checks"] if r["path"] != rec(report_path)["path"]] + [failed]
support = {r["path"]: r for r in spec["patches"][0]["supporting_records"]}
for r in (failed, rec(prior), rec(OUT / "provenance_11_readonly_validation.json"), rec(Path(__file__))):
    support[r["path"]] = r
spec["patches"][0]["supporting_records"] = sorted(support.values(), key=lambda r: r["path"])
spec["previous_unfrozen_draft"] = rec(prior)
assert spec["frozen"] is False and spec["root_approval"] is None


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


items = {r["path"]: r for r in records(spec)}
for value in items.values():
    actual = rec(ROOT / value["path"])
    assert actual["sha256"] == value["sha256"] and ("bytes" not in value or value["bytes"] == actual["bytes"])
draft = save("root_spec_11.draft_final_unfrozen.json", spec)
save("final_unfrozen_11_readonly_validation.json", {
    "format": "sn100-integration11-final-unfrozen-input-check-v1", "prepared_at": datetime.now(timezone.utc).isoformat(),
    "draft": draft, "verified_record_paths": len(items), "all_record_bytes_match": True,
    "success_retry_not_classified_as_failed_entry": True, "ledger_executed": False,
    "promotion_terminal_written": False, "frozen": False})
print(json.dumps({"draft": draft, "record_paths_verified": len(items), "frozen": False, "ledger_executed": False}))
