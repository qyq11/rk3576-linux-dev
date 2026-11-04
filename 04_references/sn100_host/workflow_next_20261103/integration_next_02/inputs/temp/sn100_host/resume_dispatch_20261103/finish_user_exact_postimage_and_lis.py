"""Preserve the failed User byte comparison, correct only line endings, then adopt LIS."""
import hashlib
import json
import subprocess
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
PROGRAM = ROOT / "06_programs/sn100_host"
ADOPTIONS = ROOT / "temp/sn100_host/workflow_next_20261103/integration_10_source_acceptance_01"
USER_OUT = ADOPTIONS / "user_init_04"
USER = ROOT / "temp/sn100_host/workflow_night_20261102/user_task_origin_init_revision_04_20261103_01"
FIX = USER_OUT / "line_ending_correction_01"
LIS = ROOT / "temp/sn100_host/lis_profile_queue_candidate_20261103_01"
LIS_OUT = ADOPTIONS / "lis_profile_queue"
CHANGED_ENDINGS = set("""original_files/original_ini.cpp original_files/original_ini.h
reagents/original_reagent_level_task.cpp reagents/original_task_reagent_display.cpp
reagents/original_task_reagent_display.h reagents/reagents_page_task.cpp
samples/original_task_reagent_receipt.h samples/original_task_reagent_receipt_writer.cpp
shell/task_reagent_context.h shell/task_reagent_shell.cpp""".split())
LIS_FILES = {"lis_transport/lis_connection_controller.cpp", "lis_transport/lis_connection_controller.h",
             "lis_transport/lis_connection_values.h"}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_map(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def save(folder, name, value):
    path = folder / name
    assert not path.exists(), path
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def actual(expected):
    return {name: sha(PROGRAM / name) for name in expected}


expected = read_map(USER_OUT / "expected_after.json")
failed = read_map(USER_OUT / "source_after.json")
assert len(expected) == 685 and expected.keys() == failed.keys()
assert actual(failed) == failed, "Formal source changed after the failed comparison."
assert {name for name in expected if expected[name] != failed[name]} == CHANGED_ENDINGS
inspection = []
for name in sorted(CHANGED_ENDINGS):
    left = (PROGRAM / name).read_bytes()
    right = (USER / "candidate/06_programs/sn100_host" / name).read_bytes()
    assert hashlib.sha256(right).hexdigest() == expected[name], name
    assert left.replace(b"\r\n", b"\n") == right.replace(b"\r\n", b"\n"), name
    inspection.append({"path": name, "actual_sha256": hashlib.sha256(left).hexdigest(),
                       "approved_sha256": expected[name], "equal_after_crlf_to_lf": True,
                       "actual_crlf": left.count(b"\r\n"), "approved_crlf": right.count(b"\r\n"),
                       "actual_lf": left.count(b"\n"), "approved_lf": right.count(b"\n")})
assert not FIX.exists() and not LIS_OUT.exists()
FIX.mkdir()
save(FIX, "postimage_mismatch_inspection.json", inspection)
save(FIX, "first_attempt_result.json", {
    "entry": "temp/sn100_host/resume_dispatch_20261103/accept_user_and_lis_sources.py",
    "actual_exit": 1, "git_check_actual_exit": 0, "git_apply_actual_exit": 0,
    "failure": "10 approved postimages differ only in CRLF/LF bytes.",
    "lis_phase_started": False, "original_logs_and_failed_source_map_preserved": True,
})
for name in sorted(CHANGED_ENDINGS):
    backup = FIX / "before" / name
    backup.parent.mkdir(parents=True, exist_ok=True)
    backup.write_bytes((PROGRAM / name).read_bytes())
    (PROGRAM / name).write_bytes((USER / "candidate/06_programs/sn100_host" / name).read_bytes())
user_actual = actual(expected)
assert user_actual == expected
save(FIX, "source_after.json", user_actual)
save(USER_OUT, "acceptance.json", {
    "schema": "sn100-root-narrow-source-adoption-v1", "phase": "user_init_04",
    "time_utc": datetime.now(timezone.utc).isoformat(), "source_files": len(user_actual),
    "modified_files": 20, "actual_source_equals_expected": True,
    "source_map": str((FIX / "source_after.json").relative_to(ROOT)),
    "source_map_sha256": sha(FIX / "source_after.json"),
    "patch_sha256": sha(USER / "packaging_01/user_task_origin_shared_initialization_on7cad.patch"),
    "first_entry_actual_exit": 1, "git_check_actual_exit": 0, "git_apply_actual_exit": 0,
    "line_ending_correction": sorted(CHANGED_ENDINGS), "byte_correction_completed": True,
    "compiled": False, "rk_verified": False, "board_operated": False,
})
print(json.dumps({"phase": "user_init_04", "source_files_verified": len(user_actual),
                  "source_map_sha256": sha(FIX / "source_after.json"),
                  "exact_byte_correction_completed": True}, ensure_ascii=False), flush=True)

lis_before, lis_after = read_map(LIS / "source_before.json"), read_map(LIS / "source_after.json")
assert len(lis_before) == 684 and lis_before.keys() == lis_after.keys()
assert {name for name in lis_before if lis_before[name] != lis_after[name]} == LIS_FILES
patch = LIS / "lis_profile_queue.patch"
assert sha(patch) == "5090d167d455d8f89c6c8a589da0fc4df911fbf26891b682d93b733aab5e18f5"
lis_expected = dict(user_actual)
for name in LIS_FILES:
    assert user_actual[name] == lis_before[name]
    assert sha(LIS / "active_candidate" / name) == lis_after[name]
    lis_expected[name] = lis_after[name]
LIS_OUT.mkdir()
save(LIS_OUT, "source_before.json", user_actual)
save(LIS_OUT, "expected_after.json", lis_expected)
commands = []
for phase, options in (("check", ["--check"]), ("apply", [])):
    argv = ["git", "-c", "core.autocrlf=false", "apply", *options, str(patch)]
    process = subprocess.run(argv, cwd=ROOT, capture_output=True)
    (LIS_OUT / (phase + ".stdout.log")).write_bytes(process.stdout)
    (LIS_OUT / (phase + ".stderr.log")).write_bytes(process.stderr)
    commands.append({"phase": phase, "argv": argv, "actual_exit": process.returncode})
    save(LIS_OUT, phase + "_result.json", commands[-1])
    if process.returncode:
        raise SystemExit(process.returncode)
lis_actual = actual(lis_expected)
save(LIS_OUT, "source_after.json", lis_actual)
assert lis_actual == lis_expected, "LIS postimage differs; do not bypass the source check."
save(LIS_OUT, "acceptance.json", {
    "schema": "sn100-root-narrow-source-adoption-v1", "phase": "lis_profile_queue",
    "time_utc": datetime.now(timezone.utc).isoformat(), "source_files": len(lis_actual),
    "scope": sorted(LIS_FILES), "patch_sha256": sha(patch),
    "source_map_sha256": sha(LIS_OUT / "source_after.json"), "actual_source_equals_expected": True,
    "compiled": False, "rk_verified": False, "board_operated": False, "commands": commands,
})
print(json.dumps({"phase": "lis_profile_queue", "source_files_verified": len(lis_actual),
                  "check_actual_exit": 0, "apply_actual_exit": 0,
                  "source_map_sha256": sha(LIS_OUT / "source_after.json"),
                  "compiled": False, "rk_verified": False}, ensure_ascii=False), flush=True)
