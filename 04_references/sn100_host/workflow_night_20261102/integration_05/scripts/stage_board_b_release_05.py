"""上传并核对独立发布目录；不切换当前程序，不恢复主控。"""
from pathlib import Path
import hashlib
import re
import shlex
import subprocess
import sys
import uuid

work = Path(__file__).resolve().parent
build = work / "integration_05"
package_name = (build / "packages/latest_package").read_text(encoding="utf-8").strip()
if package_name.startswith("/mnt/c/"):
    package_name = "C:/" + package_name[7:]
package = Path(package_name)
match = re.fullmatch(r"sn100_shell_([a-f0-9]{64})\.tar\.gz", package.name)
if not match or not package.is_file():
    raise RuntimeError("没有本轮已生成的有效发布包，未上传。")
release_id = match.group(1)
digest = hashlib.sha256(package.read_bytes()).hexdigest()
remote = "/home/rkshare/programs/sn100_shell/.workflow_upload_" + uuid.uuid4().hex + ".tar.gz"
options = ["-o", "BatchMode=yes", "-o", "ConnectTimeout=8", "-o", "StrictHostKeyChecking=yes",
           "-o", "HostKeyAlias=rk3576-board-b", "-o",
           "UserKnownHostsFile=C:/Users/52400/.ssh/known_hosts_rk3576_board_b"]
uploaded = subprocess.run(["scp", "-B", *options, str(package), "root@192.168.3.59:" + remote],
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
(build / "logs/upload.log").write_bytes(uploaded.stdout)
if uploaded.returncode:
    sys.stdout.buffer.write(uploaded.stdout)
    sys.exit(uploaded.returncode)
command = "bash -s -- " + " ".join(shlex.quote(value) for value in (remote, digest, release_id))
script = (work / "stage_board_b_release.sh").read_bytes().replace(b"\r\n", b"\n")
staged = subprocess.run(["ssh", *options, "root@192.168.3.59", command], input=script,
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
(build / "logs/board_b_stage.log").write_bytes(staged.stdout)
(build / "board_b_stage.exit").write_text(str(staged.returncode) + "\n", encoding="ascii")
sys.stdout.buffer.write(staged.stdout)
print("ACTUAL_EXIT=" + str(staged.returncode))
sys.exit(staged.returncode)
