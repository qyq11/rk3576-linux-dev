"""无线中断后沿同板B有线入口续保存；原255及原日志保持，不切程序。"""
from pathlib import Path
from datetime import datetime
import hashlib
import json
import os
import shlex
import subprocess
import sys

TASK = Path(__file__).resolve().parent
WORK = TASK / "integration_06"
ROOT = TASK.parents[2]
RECOVERY = WORK / "stage_network_recovery"
RELEASE = "14ce9714aaa0017c7ad78ff3f551d978672e5def79b96e817605295d7e59020c"
PACKAGE_SHA = "d454ca929dcb77c1c5e41b18f84f29cb48cc52d66d3d2d01858f204478718b2d"
CURRENT = "releases/e31491a11e2e51764ab49c2c4435ce2f6f1f136223a76323d508ed613a758270"
OPTIONS = ["-o", "BatchMode=yes", "-o", "ConnectTimeout=8", "-o", "StrictHostKeyChecking=yes",
           "-o", "HostKeyAlias=rk3576-board-b", "-o",
           "UserKnownHostsFile=C:/Users/52400/.ssh/known_hosts_rk3576_board_b"]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def record(path):
    return {"path": path.relative_to(ROOT).as_posix(), "sha256": sha(path)}


def write_new(path, value):
    with path.open("x", encoding="utf-8", newline="\n") as output:
        output.write(json.dumps(value, ensure_ascii=False, indent=2) + "\n")
        output.flush()
        os.fsync(output.fileno())


def main():
    initial = json.loads((WORK / "stage_started.json").read_text(encoding="utf-8"))
    script_path = TASK / "stage_board_b_release_06.sh"
    script = script_path.read_bytes()
    if (initial["release_id"] != RELEASE or initial["package_sha256"] != PACKAGE_SHA
            or initial["stage_script_sha256"] != sha(script_path)
            or sha(script_path) != "8af975382001058b0eb0d52b2f021d992cad4199cfd0b9bbbc26d7f6ae944809"
            or (WORK / "upload.exit").read_text().strip() != "0"
            or (WORK / "board_b_stage.exit").read_text().strip() != "255"
            or (TASK / "inspect_stage_06_interruption_02_wired.exit").read_text().strip() != "0"
            or b"FINAL_RELEASE_PRESENT=0\n" not in (TASK / "inspect_stage_06_interruption_02_wired.log").read_bytes()
            or b"STAGE_PARENT=" in (TASK / "inspect_stage_06_interruption_02_wired.log").read_bytes()):
        raise RuntimeError("原中断或同板有线检查依据不符，不自动续做。")
    RECOVERY.mkdir(exist_ok=False)
    inputs = {name: record(path) for name, path in {
        "original_started": WORK / "stage_started.json",
        "original_stage_log": WORK / "logs/board_b_stage.log",
        "original_stage_exit": WORK / "board_b_stage.exit",
        "upload_exit": WORK / "upload.exit",
        "wired_inspection_log": TASK / "inspect_stage_06_interruption_02_wired.log",
        "wired_inspection_exit": TASK / "inspect_stage_06_interruption_02_wired.exit",
        "wired_inspection_script": TASK / "inspect_stage_06_interruption_02_wired.sh",
        "stage_script": script_path,
        "resume_tool": Path(__file__).resolve(),
    }.items()}
    started = {"format": "sn100-integration-06-stage-network-recovery-v1",
               "started_at": datetime.now().astimezone().isoformat(), "board": "board_b",
               "serial": "8314b1e8ffd0321f", "ssh_host": "root@192.168.100.123",
               "host_key_alias": "rk3576-board-b", "release_id": RELEASE,
               "package_sha256": PACKAGE_SHA, "current_before": CURRENT,
               "remote_upload": initial["remote_upload"], "inputs": inputs, "complete": False}
    write_new(RECOVERY / "started.json", started)
    # 复核断线后现场仍未被其他写者接手，才发送原保存脚本。
    preflight = f"""set -Eeuo pipefail
test "$(tr -d '\\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
base=/home/rkshare/programs/sn100_shell
test "$(readlink "$base/current")" = '{CURRENT}'
test ! -e "$base/releases/{RELEASE}"
for item in "$base"/releases/.workflow_stage.*; do test ! -e "$item"; done
test "$(sha256sum '{initial['remote_upload']}' | cut -d' ' -f1)" = '{PACKAGE_SHA}'
printf 'BOARD=board_b\\nSERIAL=8314b1e8ffd0321f\\nEXISTING_UPLOAD_HASH_OK=1\\nNO_STAGE_OR_RELEASE=1\\n'
date -Is
"""
    (RECOVERY / "preflight.sh").write_text(preflight, encoding="utf-8", newline="\n")
    result = subprocess.run(["ssh", *OPTIONS, "root@192.168.100.123", "bash -s"],
                            input=preflight.encode(), stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (RECOVERY / "preflight.log").write_bytes(result.stdout)
    (RECOVERY / "preflight.exit").write_text(str(result.returncode) + "\n", encoding="ascii")
    if result.returncode:
        sys.stdout.buffer.write(result.stdout)
        return result.returncode
    command = "bash -s -- " + " ".join(shlex.quote(x) for x in (initial["remote_upload"], PACKAGE_SHA, RELEASE))
    result = subprocess.run(["ssh", *OPTIONS, "root@192.168.100.123", command],
                            input=script, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (RECOVERY / "stage.log").write_bytes(result.stdout)
    (RECOVERY / "stage.exit").write_text(str(result.returncode) + "\n", encoding="ascii")
    sys.stdout.buffer.write(result.stdout)
    print("ACTUAL_RESUMED_STAGE_EXIT=" + str(result.returncode))
    if result.returncode:
        return result.returncode
    lines = result.stdout.decode("utf-8").splitlines()
    for required in ("BOARD=board_b", "SERIAL=8314b1e8ffd0321f", "STAGED_RELEASE=" + RELEASE,
                     "CURRENT_RELEASE=" + CURRENT, "ACTIVATED=0", "PROGRAM_STARTED=0",
                     "PACKAGE_SHA256=" + PACKAGE_SHA, "STAGE_BATCH=integration_06"):
        if required not in lines:
            raise RuntimeError("实际保存日志不完整：" + required)
    if "not found" in result.stdout.decode("utf-8"):
        raise RuntimeError("实际动态库不齐，不登记成功。")
    for item in inputs.values():
        if sha(ROOT / item["path"]) != item["sha256"]:
            raise RuntimeError("续做期间原证据变动。")
    complete = {**started, "complete": True, "completed_at": datetime.now().astimezone().isoformat(),
                "original_stage_exit": 255, "actual_resumed_stage_exit": 0,
                "activation_performed": False, "program_started": False,
                "started_record": record(RECOVERY / "started.json"),
                "results": {name: record(RECOVERY / name) for name in
                            ("preflight.sh", "preflight.log", "preflight.exit", "stage.log", "stage.exit")}}
    write_new(RECOVERY / "complete.json", complete)
    print("NETWORK_RECOVERY_COMPLETE=1\nORIGINAL_255_PRESERVED=1")
    return 0


if __name__ == "__main__":
    sys.exit(main())
