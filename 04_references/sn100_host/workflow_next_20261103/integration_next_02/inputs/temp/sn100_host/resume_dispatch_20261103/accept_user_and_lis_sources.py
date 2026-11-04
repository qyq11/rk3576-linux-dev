"""Root adopts two reviewed, disjoint patches in sequence, verifying each full postimage."""
import hashlib
import json
import subprocess
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
PROGRAM = ROOT / "06_programs/sn100_host"
ADOPTIONS = ROOT / "temp/sn100_host/workflow_next_20261103/integration_10_source_acceptance_01"
INITIAL = ADOPTIONS / "cal/line_ending_correction_01/source_after.json"
USER = ROOT / "temp/sn100_host/workflow_night_20261102/user_task_origin_init_revision_04_20261103_01"
LIS = ROOT / "temp/sn100_host/lis_profile_queue_candidate_20261103_01"
USER_FILES = set("""original_files/original_ini.cpp original_files/original_ini.h
samples/original_sample_repository.cpp samples/original_sample_repository.h
reagents/original_reagent_repository.cpp reagents/original_reagent_repository.h
samples/original_sample_store.cpp samples/sample_store.h
samples/original_detection_session.cpp samples/original_detection_session.h
samples/original_task_reagent_receipt.h samples/original_task_reagent_receipt_writer.cpp
shell/task_reagent_context.h shell/task_reagent_shell.cpp shell/frmmain.h shell/device_control_shell.cpp
reagents/original_reagent_level_task.cpp reagents/original_task_reagent_display.h
reagents/original_task_reagent_display.cpp reagents/reagents_page_task.cpp""".split())
LIS_FILES = {"lis_transport/lis_connection_controller.cpp", "lis_transport/lis_connection_controller.h",
             "lis_transport/lis_connection_values.h"}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_map(path, count):
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    assert isinstance(value, dict) and len(value) == count
    for name, fingerprint in value.items():
        assert not Path(name).is_absolute() and ".." not in Path(name).parts and len(fingerprint) == 64
    return value


def actual(expected):
    return {name: sha(PROGRAM / name) for name in expected}


def save(out, name, value):
    path = out / name
    assert not path.exists(), path
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def adopt(name, previous, before, after, patch, patch_sha, allowed, candidate, reviews):
    assert sha(patch) == patch_sha
    assert before.keys() == after.keys()
    changed = {path for path in before if before[path] != after[path]}
    assert changed == allowed
    assert actual(previous) == previous, "Formal source moved; no overwrite is permitted."
    expected = dict(previous)
    for path in allowed:
        assert previous[path] == before[path], path
        assert sha(candidate / path) == after[path], path
        expected[path] = after[path]
    assert all(path.is_file() for path in reviews)
    out = ADOPTIONS / name
    assert not out.exists(), out
    out.mkdir()
    save(out, "source_before.json", previous)
    save(out, "expected_after.json", expected)
    commands = []
    for phase, options in (("check", ["--check"]), ("apply", [])):
        argv = ["git", "apply", *options, str(patch)]
        result = subprocess.run(argv, cwd=ROOT, capture_output=True)
        (out / (phase + ".stdout.log")).write_bytes(result.stdout)
        (out / (phase + ".stderr.log")).write_bytes(result.stderr)
        commands.append({"phase": phase, "argv": argv, "actual_exit": result.returncode})
        save(out, phase + "_result.json", commands[-1])
        if result.returncode:
            raise SystemExit(result.returncode)
    observed = actual(expected)
    save(out, "source_after.json", observed)
    assert observed == expected, "Full postimage differs; do not continue to the next patch."
    save(out, "acceptance.json", {
        "schema": "sn100-root-narrow-source-adoption-v1", "phase": name,
        "time_utc": datetime.now(timezone.utc).isoformat(), "patch_sha256": sha(patch),
        "review_records": {str(path.relative_to(ROOT)): sha(path) for path in reviews},
        "scope": sorted(allowed), "source_files": len(observed),
        "actual_source_equals_expected": True, "source_map_sha256": sha(out / "source_after.json"),
        "compiled": False, "rk_verified": False, "board_operated": False, "commands": commands,
    })
    print(json.dumps({"phase": name, "modified_files": len(allowed), "source_files_verified": len(observed),
                      "check_actual_exit": 0, "apply_actual_exit": 0,
                      "source_map_sha256": sha(out / "source_after.json"),
                      "compiled": False, "rk_verified": False}, ensure_ascii=False), flush=True)
    return observed


assert sha(INITIAL) == "7cad7a7e401f619276cfea032dcf3337c76a4522a1f52ba594a845b381b1ea7e"
previous = read_map(INITIAL, 685)
assert not USER_FILES & LIS_FILES
user_before = read_map(USER / "before_source_manifest.json", 685)
user_after = read_map(USER / "packaging_01/after_source_manifest.json", 685)
assert sha(USER / "packaging_01/after_source_manifest.json") == "b7e283018ed0ab01457effcb8f93920f454bb326f3e19838b88b72e8427512c9"
previous = adopt("user_init_04", previous, user_before, user_after,
    USER / "packaging_01/user_task_origin_shared_initialization_on7cad.patch",
    "269d0ad8f20a3c8978b69a12865ced365a6e9bdbb23586004fa51fc87ba2f781", USER_FILES,
    USER / "candidate/06_programs/sn100_host", [
        ROOT / "temp/sn100_host/resume_dispatch_20261103/user_init_04_configuration_independent_review.md",
        ROOT / "temp/sn100_host/resume_dispatch_20261103/user_init_04_lifecycle_independent_review.md",
        USER / "reviews/origin_initialization_review_01.md", USER / "reviews/lifecycle_initialization_review_01.md",
        USER / "handoff.md",
    ])
adopt("lis_profile_queue", previous, read_map(LIS / "source_before.json", 684),
    read_map(LIS / "source_after.json", 684), LIS / "lis_profile_queue.patch",
    "5090d167d455d8f89c6c8a589da0fc4df911fbf26891b682d93b733aab5e18f5", LIS_FILES,
    LIS / "active_candidate", [
        ROOT / "temp/sn100_host/resume_dispatch_20261103/lis_profile_queue_independent_review.md",
        LIS / "source_review.md", LIS / "handoff.md",
    ])
