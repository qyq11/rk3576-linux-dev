"""沿已核板B有线入口执行一次指定脚本；保留原失败，不覆盖日志。"""
from pathlib import Path
import subprocess
import sys

task = Path(__file__).resolve().parent
script = (task / sys.argv[1]).resolve()
script.relative_to(task)
for suffix in (".log", ".exit"):
    if script.with_suffix(suffix).exists():
        raise RuntimeError("已有同名执行结果，先核查，禁止覆盖。")
options = ["-o", "BatchMode=yes", "-o", "ConnectTimeout=8", "-o", "StrictHostKeyChecking=yes",
           "-o", "HostKeyAlias=rk3576-board-b", "-o",
           "UserKnownHostsFile=C:/Users/52400/.ssh/known_hosts_rk3576_board_b"]
guard = b"test \"$(tr -d '\\0' </proc/device-tree/serial-number)\" = 8314b1e8ffd0321f || exit 95\n"
result = subprocess.run(["ssh", *options, "root@192.168.100.123", "bash -s"],
                        input=guard + script.read_bytes().replace(b"\r\n", b"\n"),
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
script.with_suffix(".log").write_bytes(result.stdout)
script.with_suffix(".exit").write_text(str(result.returncode) + "\n", encoding="ascii")
sys.stdout.buffer.write(result.stdout)
sys.exit(result.returncode)
