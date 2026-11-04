"""Root-only adoption of the independently reviewed B1/User04 four-file merge."""
import hashlib
import json
import subprocess
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
PROGRAM = ROOT / "06_programs/sn100_host"
CHAIN = ROOT / "temp/sn100_host/workflow_next_20261103/integration_10_source_acceptance_01"
INITIAL = CHAIN / "observer_04/source_after.json"
MERGE = ROOT / "temp/sn100_host/resume_dispatch_20261103/ascii_b1_root_merge_01"
OUT = CHAIN / "ascii_b1"
PATCH = MERGE / "ascii_b1_on_observer_04.patch"
FILES = {"instrument/instrument_page.cpp", "instrument/instrument_page.h",
         "shell/frmmain.h", "shell/device_control_shell.cpp"}
REVIEWS = [ROOT / "temp/sn100_host/resume_dispatch_20261103" / name for name in (
    "ascii_b1_business_independent_review.md", "ascii_b1_lifecycle_independent_review.md",
    "ascii_b1_merged_business_review.md", "ascii_b1_merged_lifecycle_review.md")]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_map(path):
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    assert isinstance(value, dict) and len(value) == 687
    for name, fingerprint in value.items():
        assert not Path(name).is_absolute() and ".." not in Path(name).parts and len(fingerprint) == 64
    return value


def actual(expected):
    return {name: sha(PROGRAM / name) for name in expected}


def save(name, value):
    path = OUT / name
    assert not path.exists(), path
    path.write_bytes((json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8"))


assert sha(INITIAL) == "b688f7a8c9c888482bf53b9520239ec9618c61a2da5a701d526387e644add5d3"
assert sha(PATCH) == "b2f6c407df563571bd90f73c965b31705069a023d3b9e0a4b581fc67c3dbe106"
assert sha(MERGE / "expected_source_after_687.json") == "f741feea28fcb6a2bcff8d6c2bda49faa67d3b72e3508062e5f1e368b2422b8b"
before, expected = load_map(INITIAL), load_map(MERGE / "expected_source_after_687.json")
assert before.keys() == expected.keys()
assert {name for name in before if before[name] != expected[name]} == FILES
assert actual(before) == before, "Formal source moved; do not overwrite."
assert all(review.is_file() for review in REVIEWS)
for name in FILES:
    assert sha(MERGE / "tree" / name) == expected[name], name
assert not OUT.exists()
OUT.mkdir()
save("source_before.json", before)
save("expected_after.json", expected)
commands = []
for phase, options in (("check", ["--check"]), ("apply", [])):
    argv = ["git", "-c", "core.autocrlf=false", "apply", *options, str(PATCH)]
    result = subprocess.run(argv, cwd=ROOT, capture_output=True)
    (OUT / (phase + ".stdout.log")).write_bytes(result.stdout)
    (OUT / (phase + ".stderr.log")).write_bytes(result.stderr)
    commands.append({"phase": phase, "argv": argv, "actual_exit": result.returncode})
    save(phase + "_result.json", commands[-1])
    if result.returncode:
        raise SystemExit(result.returncode)
observed = actual(expected)
save("source_after.json", observed)
assert observed == expected, "The actual full postimage differs from the reviewed merge."
save("acceptance.json", {
    "schema": "sn100-root-narrow-source-adoption-v1", "phase": "ascii_b1",
    "time_utc": datetime.now(timezone.utc).isoformat(), "source_files": len(observed),
    "scope": sorted(FILES), "actual_source_equals_expected": True,
    "source_map_sha256": sha(OUT / "source_after.json"), "patch_sha256": sha(PATCH),
    "reviews": {str(path.relative_to(ROOT)): sha(path) for path in REVIEWS},
    "supported_exact_payloads": ["Qj", "Qk", "QH", "Ts", "Tp[0]", "Mz"],
    "full_manual_compatibility": False, "compiled": False, "rk_verified": False,
    "board_operated": False, "commands": commands,
})
print(json.dumps({"phase": "ascii_b1", "modified_files": 4, "source_files_verified": len(observed),
                  "check_actual_exit": 0, "apply_actual_exit": 0,
                  "source_map_sha256": sha(OUT / "source_after.json"),
                  "compiled": False, "rk_verified": False}, ensure_ascii=False))
