"""Keep the first failed comparison; restore only two approved byte postimages."""
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
PROGRAM = ROOT / "06_programs/sn100_host"
OUT = ROOT / "temp/sn100_host/workflow_next_20261103/integration_10_source_acceptance_01/cal"
FIX = OUT / "line_ending_correction_01"
CANDIDATE = ROOT / "temp/task_reagent_cal_metadata_candidate_01/candidate/06_programs/sn100_host"
allowed = {"reagents/reagents.pri", "reagents/original_reagent_page_metadata.h"}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path, value):
    assert not path.exists(), path
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


expected = json.loads((OUT / "expected_after.json").read_text(encoding="utf-8"))
failed = json.loads((OUT / "source_after.json").read_text(encoding="utf-8"))
inspection = json.loads((OUT / "postimage_mismatch_inspection.json").read_text(encoding="utf-8"))
assert {row["path"] for row in inspection} == allowed
assert all(row["equal_after_crlf_to_lf"] and row["current_equals_failed_snapshot"] for row in inspection)
assert {name: sha(PROGRAM / name) for name in failed} == failed
for name in allowed:
    before, after = (PROGRAM / name).read_bytes(), (CANDIDATE / name).read_bytes()
    assert before.replace(b"\r\n", b"\n") == after.replace(b"\r\n", b"\n")
    assert sha(CANDIDATE / name) == expected[name]
assert not FIX.exists()
FIX.mkdir()
save(FIX / "first_attempt_result.json", {
    "entry": "temp/sn100_host/resume_dispatch_20261103/accept_cal_source.py",
    "actual_exit": 1, "git_check_actual_exit": 0, "git_apply_actual_exit": 0,
    "failure": "Two working-tree postimages differ only in CRLF/LF bytes from the approved candidate.",
    "mismatch_paths": sorted(allowed), "original_comparison_and_logs_preserved": True,
})
for name in allowed:
    backup = FIX / "before" / name
    backup.parent.mkdir(parents=True, exist_ok=True)
    backup.write_bytes((PROGRAM / name).read_bytes())
    (PROGRAM / name).write_bytes((CANDIDATE / name).read_bytes())
actual = {name: sha(PROGRAM / name) for name in expected}
assert actual == expected
save(FIX / "source_after.json", actual)
save(OUT / "acceptance.json", {
    "schema": "sn100-root-narrow-source-adoption-v1",
    "time_utc": datetime.now(timezone.utc).isoformat(),
    "source_files": len(actual), "actual_source_equals_expected": True,
    "source_map": str((FIX / "source_after.json").relative_to(ROOT)),
    "source_map_sha256": sha(FIX / "source_after.json"),
    "first_entry_actual_exit": 1, "git_check_actual_exit": 0, "git_apply_actual_exit": 0,
    "line_ending_correction": sorted(allowed), "correction_actual_exit": 0,
    "compiled": False, "rk_verified": False, "board_operated": False,
})
print(json.dumps({"accepted_files": 17, "files_verified": len(actual),
                  "source_map": str((FIX / "source_after.json").relative_to(ROOT)),
                  "source_map_sha256": sha(FIX / "source_after.json"),
                  "first_entry_actual_exit": 1, "correction_actual_exit": 0,
                  "compiled": False, "rk_verified": False}, ensure_ascii=False))
