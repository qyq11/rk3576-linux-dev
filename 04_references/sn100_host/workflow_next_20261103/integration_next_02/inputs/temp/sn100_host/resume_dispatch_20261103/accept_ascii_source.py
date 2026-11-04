"""Root-only narrow source adoption. This never builds or runs a product."""
import hashlib
import json
import subprocess
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
PROGRAM = ROOT / "06_programs/sn100_host"
CANDIDATE = ROOT / "temp/sn100_host/communication_ascii_candidate_20261103_02_r1"
EVIDENCE = ROOT / "temp/sn100_host/workflow_next_20261103/integration_10_source_acceptance_01/ascii"
BEFORE = ROOT / "temp/sn100_host/workflow_night_20261102/integration_09_revision_03_review/source_manifest_after.json"
REPORT = ROOT / "temp/sn100_host/resume_dispatch_20261103/ascii_a2_r1_independent_review.md"
PATCH = CANDIDATE / "communication_ascii_A2_r1.patch"
ALLOWED = {"instrument/instrument_page.cpp", "instrument/instrument_page.h"}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_map(path):
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    assert isinstance(value, dict) and len(value) == 684
    for name, fingerprint in value.items():
        assert not Path(name).is_absolute() and ".." not in Path(name).parts
        assert len(fingerprint) == 64
    return value


def actual_map(expected):
    return {name: sha(PROGRAM / name) for name in expected}


def save(name, value):
    path = EVIDENCE / name
    assert not path.exists(), path
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


assert REPORT.is_file(), "The root independent review must be delivered first."
assert sha(BEFORE) == "b0667063407c2534ece08e4063cf1e63c3a25cb8c6212066ad966a9c5d492815"
assert sha(PATCH) == "952523f3614dcd758f46af5d7c40848961cf376a261425897ff134575fd67937"
before = read_map(BEFORE)
after = read_map(CANDIDATE / "candidate_expected_684.json")
assert before.keys() == after.keys()
changed = {name for name in before if before[name] != after[name]}
assert changed == ALLOWED
assert actual_map(before) == before, "Formal source differs from the approved base; do not overwrite."
assert not EVIDENCE.exists(), "Never overwrite an earlier adoption record."
EVIDENCE.mkdir(parents=True)
save("source_before.json", before)
save("expected_after.json", after)
commands = []
for phase, args in (
    ("check", ["git", "apply", "--check", "--directory=06_programs/sn100_host", str(PATCH)]),
    ("apply", ["git", "apply", "--directory=06_programs/sn100_host", str(PATCH)]),
):
    process = subprocess.run(args, cwd=ROOT, capture_output=True)
    (EVIDENCE / (phase + ".stdout.log")).write_bytes(process.stdout)
    (EVIDENCE / (phase + ".stderr.log")).write_bytes(process.stderr)
    commands.append({"phase": phase, "argv": args, "actual_exit": process.returncode})
    save(phase + "_result.json", commands[-1])
    if process.returncode != 0:
        raise SystemExit(process.returncode)
actual = actual_map(after)
save("source_after.json", actual)
assert actual == after, "Actual source does not equal the expected postimage."
save("acceptance.json", {
    "schema": "sn100-root-narrow-source-adoption-v1",
    "time_utc": datetime.now(timezone.utc).isoformat(),
    "candidate": str(CANDIDATE.relative_to(ROOT)),
    "patch_sha256": sha(PATCH),
    "root_review_sha256": sha(REPORT),
    "scope": sorted(changed),
    "source_files": len(actual),
    "actual_source_equals_expected": True,
    "manual_send_connected": False,
    "compiled": False,
    "rk_verified": False,
    "board_operated": False,
    "commands": commands,
})
print(json.dumps({"accepted": sorted(changed), "files_verified": len(actual),
                  "check_actual_exit": 0, "apply_actual_exit": 0,
                  "compiled": False, "rk_verified": False}, ensure_ascii=False))
