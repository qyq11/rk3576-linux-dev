"""电脑侧发送板B容量资料只读核查，不在板端使用Python。"""
from pathlib import Path
import subprocess
import sys

task = Path(__file__).resolve().parent
options = ["-o", "BatchMode=yes", "-o", "ConnectTimeout=8", "-o", "StrictHostKeyChecking=yes",
           "-o", "HostKeyAlias=rk3576-board-b", "-o",
           "UserKnownHostsFile=C:/Users/52400/.ssh/known_hosts_rk3576_board_b"]
result = subprocess.run(["ssh", *options, "root@192.168.3.59", "bash -s"],
                        input=(task / "board_b_capacity_files_readonly.sh").read_bytes().replace(b"\r\n", b"\n"),
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
(task / "board_b_capacity_files_readonly.log").write_bytes(result.stdout)
(task / "board_b_capacity_files_readonly.exit").write_text(str(result.returncode) + "\n", encoding="ascii")
sys.stdout.buffer.write(result.stdout)
sys.exit(result.returncode)
