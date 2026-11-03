"""发送指定的本轮脚本；先固定板卡身份，分别保存输出与退出码。"""
from pathlib import Path
import subprocess
import sys

task = Path(__file__).resolve().parent
script = (task / sys.argv[1]).resolve()
script.relative_to(task)
options = ["-o", "BatchMode=yes", "-o", "ConnectTimeout=8", "-o", "StrictHostKeyChecking=yes",
           "-o", "HostKeyAlias=rk3576-board-b", "-o",
           "UserKnownHostsFile=C:/Users/52400/.ssh/known_hosts_rk3576_board_b"]
guard = b"test \"$(tr -d '\\0' </proc/device-tree/serial-number)\" = 8314b1e8ffd0321f || exit 95\n"
result = subprocess.run(["ssh", *options, "root@192.168.3.59", "bash -s"],
                        input=guard + script.read_bytes().replace(b"\r\n", b"\n"),
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
script.with_suffix(".log").write_bytes(result.stdout)
script.with_suffix(".exit").write_text(str(result.returncode) + "\n", encoding="ascii")
sys.stdout.buffer.write(result.stdout)
sys.exit(result.returncode)
