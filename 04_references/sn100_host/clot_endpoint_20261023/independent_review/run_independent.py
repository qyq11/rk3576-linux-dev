#!/usr/bin/env python3
"""Independent synthetic mathematical review; no patient or instrument data."""
import json
import math
from pathlib import Path
import subprocess
import sys

binary = Path(sys.argv[1]).resolve()
review = Path(__file__).resolve().parent
run_tag = sys.argv[2] if len(sys.argv) > 2 else "round1"
if not run_tag.isalnum():
    raise RuntimeError("Review run name must be alphanumeric")
run = review / "cases" / run_tag
run.mkdir(parents=True, exist_ok=True)
summaries = []

def gompertz(t):
    return 100.0 * math.exp(-math.exp(-(t - 50.0) / 8.0))

def logistic(t, center=50.0, width=5.0):
    return 100.0 / (1.0 + math.exp(-(t - center) / width))

def run_case(name, points, *, direction="rising", parameters=None, expected=None):
    original = {
        "schema": "sn100-clot-endpoint-input-v1",
        "data_kind": "synthetic",
        "time_reference": "seconds-since-reagent-addition",
        "complete_curve": True,
        "direction": direction,
        "parameters": parameters or {},
        "description": "Independent anonymous mathematical review: " + name,
        "points": points,
    }
    inp = run / (name + "_input.json")
    out = run / (name + "_result.json")
    if inp.exists() or out.exists():
        raise RuntimeError("Refuse to replace review evidence: " + name)
    inp.write_text(json.dumps(original, allow_nan=False, indent=2) + "\n", encoding="utf-8")
    proc = subprocess.run([str(binary), "--input", str(inp), "--output", str(out)],
                          capture_output=True, text=True, timeout=15)
    data = json.loads(out.read_text()) if out.exists() else None
    record = {"case": name, "exit_code": proc.returncode, "stderr": proc.stderr.strip()}
    if data:
        for key in ("percentage", "maximum_derivative"):
            value = data[key]
            record[key] = {k: value[k] for k in
                ("candidate_found", "time_seconds", "error_code", "warnings") if k in value}
    checks = []
    for method, wanted in (expected or {}).items():
        result = data[method] if data else None
        if isinstance(wanted, bool):
            checks.append(bool(result) and result["candidate_found"] == wanted)
        else:
            center, tolerance = wanted
            checks.append(bool(result) and result["candidate_found"] and
                          abs(result["time_seconds"] - center) <= tolerance)
    record["passed"] = all(checks)
    summaries.append(record)

uniform = [[float(i), gompertz(i)] for i in range(121)]
expected_pct = 50.0 - 8.0 * math.log(math.log(2.0))
run_case("asymmetric_gompertz", uniform, expected={
    "percentage": (expected_pct, .12), "maximum_derivative": (50.0, 1.0)})
run_case("falling_gompertz", [[t, 100-y] for t, y in uniform],
    direction="falling", expected={
    "percentage": (expected_pct, .12), "maximum_derivative": (50.0, 1.0)})
run_case("reagent_origin_preserved", [[t+10, y] for t, y in uniform],
    expected={"percentage": (expected_pct+10, .12),
              "maximum_derivative": (60.0, 1.0)})
run_case("seconds_scaled", [[t*3, y] for t, y in uniform],
    expected={"percentage": (expected_pct*3, .36),
              "maximum_derivative": (150.0, 3.0)})
irregular = []
t = 0.0
intervals = (.25, .75, 1.25, .25)
while t <= 120:
    irregular.append([t, gompertz(t)])
    t += intervals[(len(irregular)-1) % len(intervals)]
run_case("nonuniform_time", irregular, expected={
    "percentage": (expected_pct, .2), "maximum_derivative": (50.0, 1.5)})
run_case("linear_has_no_reference_plateau",
    [[float(i), float(i)] for i in range(201)],
    expected={"percentage": False, "maximum_derivative": False})
run_case("unfinished_logistic",
    [[float(i), logistic(i)] for i in range(51)],
    expected={"percentage": False, "maximum_derivative": False})
run_case("flat_curve", [[float(i), 10.0] for i in range(50)],
    expected={"percentage": False, "maximum_derivative": False})
run_case("low_amplitude",
    [[float(i), logistic(i)*1e-11] for i in range(121)],
    expected={"percentage": False, "maximum_derivative": False})
run_case("opposite_direction", uniform, direction="falling",
    expected={"percentage": False, "maximum_derivative": False})
run_case("two_separated_rate_peaks",
    [[float(i), .5*logistic(i, 35, 3)+.5*logistic(i, 75, 3)] for i in range(121)],
    expected={"maximum_derivative": False})
run_case("window_before_main_transition", uniform,
    parameters={"search_start_seconds": 0, "search_end_seconds": 40},
    expected={"percentage": False, "maximum_derivative": False})
run_case("window_after_main_transition", uniform,
    parameters={"search_start_seconds": 65, "search_end_seconds": 120},
    expected={"percentage": False, "maximum_derivative": False})
duplicates = [p[:] for p in uniform]
duplicates[60][0] = duplicates[59][0]
run_case("duplicate_time", duplicates,
    expected={"percentage": False, "maximum_derivative": False})
negative = [p[:] for p in uniform]
negative[0][0] = -1.0
run_case("negative_reagent_time", negative,
    expected={"percentage": False, "maximum_derivative": False})
summary = {
    "evidence": "independent-synthetic-mathematical-review",
    "clinical_validation": False,
    "al4a_equivalence": False,
    "expected_gompertz_half_time_seconds": expected_pct,
    "expected_gompertz_rate_peak_seconds": 50,
    "passed": all(r["passed"] for r in summaries),
    "cases": summaries,
}
out_summary = review / ("independent_summary_" + run_tag + ".json")
if out_summary.exists():
    raise RuntimeError("Refuse to replace summary evidence")
out_summary.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
print(json.dumps(summary, indent=2))
sys.exit(0 if summary["passed"] else 1)
