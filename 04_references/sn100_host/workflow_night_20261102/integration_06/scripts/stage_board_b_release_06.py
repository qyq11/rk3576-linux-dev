"""根执行后上传第06批到板B独立目录并核对；不切换当前版本或启动产品。"""
from pathlib import Path
import hashlib
import json
import os
import re
import shlex
import subprocess
import sys
import uuid


def main():
    task = Path(__file__).resolve().parent
    work = task / "integration_06"
    if any((work / name).exists() for name in ("stage_started.json", "board_b_stage.exit")):
        raise RuntimeError("本批已有板B操作记录，先人工核查，不自动重跑。")
    complete = json.loads((work / "build_complete.json").read_text(encoding="utf-8"))
    if ((work / "build_exit.txt").read_text().strip() != "0"
            or complete.get("arm64_build_and_package_passed") is not True
            or "ARM64_INTEGRATION_06_BUILD_AND_PACKAGE_PASSED=1" not in (work / "logs/build.log").read_text(encoding="utf-8")):
        raise RuntimeError("第06批完整构建未通过，未连接板卡。")
    value = (work / "packages/latest_package").read_text(encoding="utf-8").strip()
    if os.name == "nt" and value.startswith("/mnt/c/"):
        value = "C:/" + value[7:]
    package = Path(value)
    package.resolve().relative_to((work / "packages").resolve())
    match = re.fullmatch(r"sn100_shell_([a-f0-9]{64})\.tar\.gz", package.name)
    digest = hashlib.sha256(package.read_bytes()).hexdigest()
    if package.is_symlink() or not match or package.name != complete["package"] or digest != complete["package_sha256"]:
        raise RuntimeError("没有本轮已完整构建且指纹相符的程序包。")
    release = match.group(1)
    remote = "/home/rkshare/programs/sn100_shell/.workflow_upload_" + uuid.uuid4().hex + ".tar.gz"
    # 已核SSH指纹只对应板B；板上脚本在解包前再核硬件序列号。
    known_hosts = ("C:/Users/52400/.ssh/known_hosts_rk3576_board_b" if os.name == "nt"
                   else "/mnt/c/Users/52400/.ssh/known_hosts_rk3576_board_b")
    options = ["-o", "BatchMode=yes", "-o", "ConnectTimeout=8", "-o", "StrictHostKeyChecking=yes",
               "-o", "HostKeyAlias=rk3576-board-b", "-o", "UserKnownHostsFile=" + known_hosts]
    script = (task / "stage_board_b_release_06.sh").read_bytes()
    if b"\r" in script:
        raise RuntimeError("板端脚本不是已核LF原文。")
    started = {"batch": "integration_06", "board": "board_b", "serial": "8314b1e8ffd0321f",
               "release_id": release, "package_sha256": digest, "remote_upload": remote,
               "stage_script_sha256": hashlib.sha256(script).hexdigest(), "complete": False}
    with (work / "stage_started.json").open("x", encoding="utf-8") as output:
        output.write(json.dumps(started, ensure_ascii=False, indent=2) + "\n")
        output.flush()
        os.fsync(output.fileno())
    uploaded = subprocess.run(["scp", "-B", *options, str(package), "root@192.168.3.59:" + remote],
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (work / "logs/upload.log").write_bytes(uploaded.stdout)
    (work / "upload.exit").write_text(str(uploaded.returncode) + "\n", encoding="ascii")
    if uploaded.returncode:
        sys.stdout.buffer.write(uploaded.stdout)
        return uploaded.returncode
    command = "bash -s -- " + " ".join(shlex.quote(x) for x in (remote, digest, release))
    staged = subprocess.run(["ssh", *options, "root@192.168.3.59", command], input=script,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (work / "logs/board_b_stage.log").write_bytes(staged.stdout)
    (work / "board_b_stage.exit").write_text(str(staged.returncode) + "\n", encoding="ascii")
    sys.stdout.buffer.write(staged.stdout)
    print("ACTUAL_EXIT=" + str(staged.returncode))
    return staged.returncode


if __name__ == "__main__":
    sys.exit(main())
