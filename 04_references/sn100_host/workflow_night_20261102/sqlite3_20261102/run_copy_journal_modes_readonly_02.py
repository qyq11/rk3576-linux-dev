"""电脑侧发送只读脚本并保存真实退出码；板端不使用Python。"""
from pathlib import Path
import subprocess
import sys

task = Path(__file__).resolve().parent
options = ["-o", "BatchMode=yes", "-o", "ConnectTimeout=8", "-o", "StrictHostKeyChecking=yes",
           "-o", "HostKeyAlias=rk3576-board-b", "-o",
           "UserKnownHostsFile=C:/Users/52400/.ssh/known_hosts_rk3576_board_b"]
result = subprocess.run(["ssh", *options, "root@192.168.3.59", "bash -s"],
                        input=(task / "board_b_copy_journal_modes_readonly_02.sh").read_bytes().replace(b"\r\n", b"\n"),
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
(task / "board_b_copy_journal_modes_readonly_02.log").write_bytes(result.stdout)
(task / "board_b_copy_journal_modes_readonly_02.exit").write_text(str(result.returncode) + "\n", encoding="ascii")
sys.stdout.buffer.write(result.stdout)
sys.exit(result.returncode)
