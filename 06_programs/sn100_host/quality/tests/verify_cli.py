#!/usr/bin/env python3
"""Developer-host CLI checks; never installed or run on the RK board."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    parser.add_argument("fixture", type=Path)
    parser.add_argument("work_parent", type=Path)
    parser.add_argument("--compare-results", type=Path)
    args = parser.parse_args()
    executable = args.executable.resolve(strict=True)
    fixture_path = args.fixture.resolve(strict=True)
    work_parent = args.work_parent.resolve(strict=True)
    work = Path(tempfile.mkdtemp(prefix="cli_checks_", dir=work_parent))
    fixture = json.loads(fixture_path.read_text(encoding="utf-8"))
    checks = []

    def run(name, source=fixture_path, expected_exit=0, extra=(), output=None):
        output = output or work / name
        command = [str(executable), "--input", str(source), "--output", str(output), *extra]
        process = subprocess.run(command, text=True, encoding="utf-8", errors="replace", capture_output=True, timeout=30)
        (work / (name + ".stdout.txt")).write_text(process.stdout, encoding="utf-8")
        (work / (name + ".stderr.txt")).write_text(process.stderr, encoding="utf-8")
        assert process.returncode == expected_exit, (name, process.returncode, process.stderr, process.stdout)
        checks.append({"name": name, "exit_code": process.returncode, "expected_exit": expected_exit})
        return output

    def source(name, data):
        path = work / (name + ".json")
        path.write_text(json.dumps(data, ensure_ascii=False), encoding="utf-8")
        return path

    def document(values, identifier="boundary"):
        return {"schema": "sn100-quality-statistics-input-v1", "evidence": "anonymous-handcrafted", "series": [{"id": identifier, "values": values}]}

    first = run("golden_first_process")
    second = run("golden_second_process")
    actual = json.loads((first / "results.json").read_text(encoding="utf-8"))
    assert actual == json.loads((second / "results.json").read_text(encoding="utf-8")), "independent process mismatch"
    assert len(actual["series"]) == len(fixture["series"])
    for supplied, record in zip(fixture["series"], actual["series"]):
        assert record["id"] == supplied["id"] and record["input_values"] == supplied["values"]
        statistics = record["statistics"]
        assert statistics["ok"] and statistics["count"] == len(supplied["values"])
        assert statistics["cv_unit"] == "ratio-not-percent"
        for key, expected in supplied["expected"].items():
            observed = statistics[key]
            if expected is None or isinstance(expected, bool):
                assert observed is expected, (supplied["id"], key, observed, expected)
            else:
                assert math.isclose(observed, expected, rel_tol=1e-12, abs_tol=1e-12), (supplied["id"], key, observed, expected)

    comparison_equal = None
    if args.compare_results:
        other = json.loads(args.compare_results.resolve(strict=True).read_text(encoding="utf-8"))
        assert len(actual["series"]) == len(other["series"])
        for record, other_record in zip(actual["series"], other["series"]):
            assert record["id"] == other_record["id"] and record["input_values"] == other_record["input_values"]
            for key, observed in record["statistics"].items():
                other_value = other_record["statistics"][key]
                if isinstance(observed, (int, float)) and not isinstance(observed, bool):
                    assert math.isclose(observed, other_value, rel_tol=1e-12, abs_tol=1e-12), (record["id"], key, observed, other_value)
                else:
                    assert observed == other_value, (record["id"], key, observed, other_value)
        comparison_equal = actual == other

    before = hashlib.sha256((first / "results.json").read_bytes()).hexdigest()
    run("existing_output_refused", output=first, expected_exit=2)
    assert hashlib.sha256((first / "results.json").read_bytes()).hexdigest() == before
    run("relative_output_refused", output=Path("quality_relative_refused"), expected_exit=2)
    run("empty_series", source("empty_series", document([])), expected_exit=4)
    run("variance_overflow", source("variance_overflow", document([-1e200, 1e200])), expected_exit=4)
    run("sum_overflow", source("sum_overflow", document([1e308, 1e308])), expected_exit=4)
    for name, values in [("null_sample", [None]), ("bool_sample", [True]), ("string_sample", ["1"]), ("sample_budget", [1] * 10001)]:
        run(name, source(name, document(values)), expected_exit=2)
    duplicate = document([1, 2])
    duplicate["series"].append(dict(duplicate["series"][0]))
    run("duplicate_ids", source("duplicate_ids", duplicate), expected_exit=2)
    wrong_schema = document([1, 2])
    wrong_schema["evidence"] = "not-declared-anonymous"
    run("anonymous_contract", source("anonymous_contract", wrong_schema), expected_exit=2)
    malformed = work / "malformed.json"
    malformed.write_text('{"series": [NaN]}', encoding="utf-8")
    run("malformed_json", malformed, expected_exit=2)
    huge = work / "file_budget.json"
    huge.write_text(" " * (2 * 1024 * 1024 + 1), encoding="utf-8")
    run("file_budget", huge, expected_exit=2)
    run("stress_negative", expected_exit=2, extra=("--stress", "-1"))
    run("stress_over_budget", expected_exit=2, extra=("--stress", "1000001"))
    run("output_parent_missing", output=work / "missing_parent" / "child", expected_exit=2)
    boundary = run("large_singleton_stress", source("large_singleton_stress", document([1e308])), extra=("--stress", "2"))
    measurement = json.loads((boundary / "measurement.json").read_text(encoding="utf-8"))
    assert measurement["stress_last_mean"] == 1e308 and measurement["stress_operations"] == 2
    assert measurement["failures"] == 0
    summary = {"schema": "sn100-quality-cli-verification-v1", "executable": str(executable),
               "fixture": str(fixture_path), "work_directory": str(work), "checks": checks,
               "check_count": len(checks), "golden_series": len(fixture["series"]),
               "independent_process_equal": True, "existing_output_unchanged": True,
               "cross_platform_exact_json_equal": comparison_equal,
               "cross_platform_comparison": str(args.compare_results) if args.compare_results else None,
               "cross_platform_numeric_tolerance": "relative=1e-12, absolute=1e-12"}
    (work / "verification.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
