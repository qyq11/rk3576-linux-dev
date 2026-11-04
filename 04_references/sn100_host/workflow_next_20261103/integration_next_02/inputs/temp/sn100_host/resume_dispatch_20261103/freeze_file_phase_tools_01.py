"""Root freezes an isolated copy of the nine reviewed file-phase tools."""
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
TASK = ROOT / "temp/sn100_host/workflow_next_20261103"
SOURCE = TASK / "build_archive_candidate_03"
DEST = TASK / "integration_10_root_frozen_01"
LEDGER = ROOT / "temp/sn100_host/resume_dispatch_20261103/root_source_ledger_final_01"
REPORT = ROOT / "temp/sn100_host/resume_dispatch_20261103/build_archive_03_targeted_review.md"
FROZEN_SPEC = ROOT / "temp/sn100_host/resume_dispatch_20261103/source_ledger_inputs_01/root_spec.frozen_01.json"
NINE = ["integration_next_tool_support.py", "build_integration_next.sh",
        "verify_integration_next_inputs.py", "integration_next_object_binding.py",
        "stage_board_b_release_next.py", "stage_board_b_release_next.sh",
        "archive_integration_next.py", "verify_and_save_integration_next.py",
        "finish_integration_next_archive.py"]
EXTRA = ["transition_release_next.sh.in", "runtime_launch_intent.template.json",
         "operation_contract.md", "README.md", "root_decision_alignment.md"]


def sha(path):
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1048576), b""):
            value.update(block)
    return value.hexdigest()


def record(path):
    return {"path": path.relative_to(ROOT).as_posix(), "sha256": sha(path), "bytes": path.stat().st_size}


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


assert not DEST.exists() and not (TASK / "integration_next_02").exists()
assert sha(LEDGER / "final_review.json") == "610685a82680694218a35e7c57b3a3890de896ab81ca9b02a9536bd959651b02"
assert sha(LEDGER / "promotion_progress.json") == "0d3384e5c76493a1a14fc48245b2665701b57b6180379418681de0cf0a31db5b"
approved = dict(re.findall(r"\|\s*([A-Za-z0-9_.-]+)\s*\|\s*([0-9a-f]{64})\s*\|", REPORT.read_text(encoding="utf-8-sig")))
for name in NINE + ["transition_release_next.sh.in", "runtime_launch_intent.template.json"]:
    assert name in approved and sha(SOURCE / name) == approved[name], name
spec = read(FROZEN_SPEC)
assert spec["frozen"] is True and spec["all_intended_source_slices_present"] is True
candidate = read(SOURCE / "integration_next_tools_config.json")
assert candidate["frozen"] is False and not any(candidate["operation_permissions"].values())
assert int((ROOT / "temp/sn100_host/resume_dispatch_20261103/source_ledger_run_01.exit").read_text().strip()) == 0

# 仅给新证据/备份命名空间保存原字节；不改已被冻结的根.gitattributes。
attributes = []
for relative in ("04_references/sn100_host/workflow_next_20261103/.gitattributes",
                 "07_backups/sn100_host/board_b/workflow_integration_20261103/.gitattributes"):
    path = ROOT / relative
    raw = b"* -text\n"
    if path.exists():
        assert path.read_bytes() == raw
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("xb") as stream:
            stream.write(raw)
    attributes.append(record(path))
assert sha(ROOT / ".gitattributes") == "c46d8032dc7311b5911dfb7e578ad70ba1258ac924b72675f8b2192c5b9d445a"

DEST.mkdir()
for name in NINE + EXTRA:
    raw = (SOURCE / name).read_bytes()
    with (DEST / name).open("xb") as stream:
        stream.write(raw)
    assert sha(DEST / name) == sha(SOURCE / name)
config = candidate
config["frozen"] = True
config["purpose"] = "第10批六片687源的完整ARM64构建和板B新目录只保存；不运行产品、不启停/切版。工具修订03，执行命名空间integration_next_02。"
config["approved_final_review_sha256"] = sha(LEDGER / "final_review.json")
config["metadata_schema"] = {"review_format": "sn100-root-real-source-final-review-v1",
                             "promotion_format": "sn100-root-real-source-promotion-v1"}
config["final_review"] = record(LEDGER / "final_review.json")
config["promotion_progress"] = record(LEDGER / "promotion_progress.json")
config["promotion_script"] = spec["ledger_script"]
for name in ("baseline_archive", "baseline_snapshot", "final_source_manifest"):
    config[name] = spec[name]
config["expected_current"] = "releases/b31f581fd240e09ee4c0a4a4eb6c4cbdd674fa9cf8897fcd170e9d0ca058d993"
config["expected_controller_exe"] = "/userdata/shared/ACA_V1.0OffLineD-500"
config["operation_permissions"] = {"allow_full_build": True, "allow_stage": True}
config["tools"] = [record(DEST / name) for name in NINE]
records = read(LEDGER / "required_cfg_evidence.json")
extra_records = [record(REPORT), record(LEDGER / "final_review.json"), record(LEDGER / "promotion_progress.json"),
                 config["observation_contract"]["root_decision"], *attributes]
for relative in ("temp/sn100_host/resume_dispatch_20261103/build_archive_next_independent_review.md",
                 "temp/sn100_host/resume_dispatch_20261103/source_ledger_independent_review.md",
                 "temp/sn100_host/resume_dispatch_20261103/source_ledger_run_01.stdout.log",
                 "temp/sn100_host/resume_dispatch_20261103/source_ledger_run_01.stderr.log",
                 "temp/sn100_host/resume_dispatch_20261103/source_ledger_run_01.exit",
                 "temp/sn100_host/root_manager_20261103/prebuild_live_20261103_01.sh",
                 "temp/sn100_host/root_manager_20261103/prebuild_live_20261103_01.log",
                 "temp/sn100_host/root_manager_20261103/prebuild_live_20261103_01.exit",
                 "temp/sn100_host/root_manager_20261103/prebuild_live_20261103_01.started",
                 "temp/sn100_host/root_manager_20261103/board08.py",
                 "05_tools/tests/sn100_host/lis_keep_running_20261103/wait_lis_observation.sh",
                 "05_tools/tests/sn100_host/lis_keep_running_20261103/review_lis_observation.py",
                 "temp/sn100_host/resume_dispatch_20261103/freeze_file_phase_tools_01.py"):
    extra_records.append(record(ROOT / relative))
extra_records.extend(record(DEST / name) for name in EXTRA)
unique = {}
for item in records + extra_records:
    actual = record(ROOT / item["path"])
    assert actual["sha256"] == item["sha256"] and actual["bytes"] == item["bytes"]
    if item["path"] in unique:
        assert unique[item["path"]] == actual
    unique[item["path"]] = actual
config["evidence"] = sorted(unique.values(), key=lambda item: item["path"])
with (DEST / "integration_next_tools_config.json").open("xb") as stream:
    stream.write((json.dumps(config, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))
note = {"schema": "sn100-root-file-phase-tool-freeze-v1", "tool_revision": "candidate_03",
        "execution_namespace": "integration_next_02", "source_count": 687,
        "config": record(DEST / "integration_next_tools_config.json"),
        "source_code_copies_equal": True, "compiled": False, "staged": False,
        "activated": False, "application_started": False,
        "explanation": "许可是真实后续操作范围，不代表操作已执行。新包启动意图在核包后另建。"}
with (DEST / "root_freeze.json").open("xb") as stream:
    stream.write((json.dumps(note, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))
print(json.dumps(note, ensure_ascii=False))
