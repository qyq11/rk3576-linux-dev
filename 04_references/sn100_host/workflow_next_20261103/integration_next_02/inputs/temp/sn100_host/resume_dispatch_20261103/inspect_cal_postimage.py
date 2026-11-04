import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[3]
out = root / "temp/sn100_host/workflow_next_20261103/integration_10_source_acceptance_01/cal"
expected = json.loads((out / "expected_after.json").read_text(encoding="utf-8"))
actual = json.loads((out / "source_after.json").read_text(encoding="utf-8"))
result = []
for name in expected:
    if expected[name] == actual[name]:
        continue
    target = root / "06_programs/sn100_host" / name
    candidate = root / "temp/task_reagent_cal_metadata_candidate_01/candidate/06_programs/sn100_host" / name
    left, right = target.read_bytes(), candidate.read_bytes()
    result.append({"path": name, "expected": expected[name], "actual": actual[name],
                   "actual_size": len(left), "candidate_size": len(right),
                   "actual_crlf": left.count(b"\r\n"), "candidate_crlf": right.count(b"\r\n"),
                   "actual_lf": left.count(b"\n"), "candidate_lf": right.count(b"\n"),
                   "equal_after_crlf_to_lf": left.replace(b"\r\n", b"\n") == right.replace(b"\r\n", b"\n"),
                   "current_equals_failed_snapshot": hashlib.sha256(left).hexdigest() == actual[name]})
path = out / "postimage_mismatch_inspection.json"
assert not path.exists()
path.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(json.dumps(result, ensure_ascii=False))
