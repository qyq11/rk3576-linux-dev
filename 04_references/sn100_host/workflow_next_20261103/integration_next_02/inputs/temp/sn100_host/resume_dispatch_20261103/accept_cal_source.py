"""Root-only adoption of the reviewed CAL metadata patch, after ASCII."""
import hashlib
import json
import subprocess
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
PROGRAM = ROOT / "06_programs/sn100_host"
BASE = ROOT / "temp/sn100_host/workflow_night_20261102/integration_09_revision_03_review/source_manifest_after.json"
PREVIOUS = ROOT / "temp/sn100_host/workflow_next_20261103/integration_10_source_acceptance_01/ascii/source_after.json"
CAL = ROOT / "temp/task_reagent_cal_metadata_candidate_01"
PATCH = CAL / "packaging_01/task_reagent_cal_metadata_onb066.patch"
POST = CAL / "packaging_01/final_source_snapshot.json"
OUT = PREVIOUS.parent.parent / "cal"
REVIEWS = [ROOT / "temp/sn100_host/resume_dispatch_20261103" / name for name in (
    "cal_candidate_resume_review.md", "cal_high_risk_independent_review.md", "cal_saved_hole_us_followup.md")]
ALLOWED = {
    "reagents/original_reagent_page_metadata.h", "reagents/original_reagent_repository.h",
    "reagents/original_reagent_repository.cpp", "reagents/original_reagent_editor.h",
    "reagents/original_reagent_editor.cpp", "reagents/reagent_store.h", "reagents/reagent_store.cpp",
    "reagents/original_reagent_store.cpp", "reagents/reagents_page.h", "reagents/reagents_page.cpp",
    "samples/original_detection_session.h", "samples/original_detection_session.cpp",
    "samples/sample_store.h", "samples/original_sample_store.cpp", "shell/main.cpp",
    "reagents/reagents.pri", "shell/build.sh",
}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def get_map(path, count):
    result = json.loads(path.read_text(encoding="utf-8-sig"))
    assert isinstance(result, dict) and len(result) == count
    for name, value in result.items():
        assert not Path(name).is_absolute() and ".." not in Path(name).parts and len(value) == 64
    return result


def actual(expected):
    return {name: sha(PROGRAM / name) for name in expected}


def save(name, value):
    target = OUT / name
    assert not target.exists(), target
    target.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


assert sha(PREVIOUS) == "747ae955c12b4bc39e6b76ab80b282445c07500d92b08a364861333ddad5f913"
assert sha(PATCH) == "075659fd929fcb2bd7a2749b10cc5d97c9c8a5a47cab0a69c3483dc1b8c220c8"
assert sha(POST) == "3c96d09c4b52a199c36d13f6cfe53b48f55a4779e8fba9ce34d16da128aab732"
assert all(review.is_file() for review in REVIEWS)
baseline, previous, cal_after = get_map(BASE, 684), get_map(PREVIOUS, 684), get_map(POST, 685)
changed = {name for name in cal_after if baseline.get(name) != cal_after[name]}
assert changed == ALLOWED and baseline.keys() <= cal_after.keys()
assert actual(previous) == previous, "Formal source moved; do not overwrite."
expected = dict(previous)
for name in changed:
    path = PROGRAM / name
    if name in baseline:
        assert previous[name] == baseline[name], name
    else:
        assert not path.exists(), name
    assert sha(CAL / "candidate/06_programs/sn100_host" / name) == cal_after[name], name
    expected[name] = cal_after[name]
assert not OUT.exists(), "Never overwrite an earlier adoption record."
OUT.mkdir(parents=True)
save("source_before.json", previous)
save("expected_after.json", expected)
commands = []
for phase, options in (("check", ["--check"]), ("apply", [])):
    args = ["git", "apply", *options, str(PATCH)]
    result = subprocess.run(args, cwd=ROOT, capture_output=True)
    (OUT / (phase + ".stdout.log")).write_bytes(result.stdout)
    (OUT / (phase + ".stderr.log")).write_bytes(result.stderr)
    commands.append({"phase": phase, "argv": args, "actual_exit": result.returncode})
    save(phase + "_result.json", commands[-1])
    if result.returncode:
        raise SystemExit(result.returncode)
observed = actual(expected)
save("source_after.json", observed)
assert observed == expected, "Combined postimage differs from the expected narrow composition."
save("acceptance.json", {
    "schema": "sn100-root-narrow-source-adoption-v1",
    "time_utc": datetime.now(timezone.utc).isoformat(),
    "patch_sha256": sha(PATCH), "previous_source_map_sha256": sha(PREVIOUS),
    "source_map_sha256": sha(OUT / "source_after.json"),
    "root_reviews": {str(path.relative_to(ROOT)): sha(path) for path in REVIEWS},
    "scope": sorted(changed), "source_files": len(observed),
    "actual_source_equals_expected": True, "compiled": False, "rk_verified": False,
    "board_operated": False, "commands": commands,
})
print(json.dumps({"accepted_files": len(changed), "files_verified": len(observed),
                  "source_map_sha256": sha(OUT / "source_after.json"),
                  "check_actual_exit": 0, "apply_actual_exit": 0,
                  "compiled": False, "rk_verified": False}, ensure_ascii=False))
