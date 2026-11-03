"""以原始字节发送只读检查，避免PowerShell向bash追加CRLF空行。"""
from pathlib import Path
import subprocess
import sys

work = Path(__file__).resolve().parent
command = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=8",
           "-o", "StrictHostKeyChecking=yes", "-o", "HostKeyAlias=rk3576-board-b",
           "-o", "UserKnownHostsFile=C:/Users/52400/.ssh/known_hosts_rk3576_board_b",
           "root@192.168.3.59", "bash", "-s"]
script = (work / "board_b_readonly_preflight.sh").read_bytes().replace(b"\r\n", b"\n")
result = subprocess.run(command, input=script, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
(work / "board_b_readonly_preflight_02.log").write_bytes(result.stdout)
(work / "board_b_readonly_preflight_02.exit").write_text(str(result.returncode) + "\n", encoding="ascii")
sys.stdout.buffer.write(result.stdout)
print("ACTUAL_EXIT=" + str(result.returncode))
sys.exit(result.returncode)
