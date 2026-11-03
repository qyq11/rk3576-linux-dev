"""在电脑WSL中强制只用密码连接板B；密码从标准输入读取，不写入文件。"""
import errno
import ipaddress
import os
import pty
import re
import select
import signal
import sys
import termios
import time

secret = sys.stdin.readline().rstrip("\n")
if not secret or "\r" in secret or "\n" in secret:
    raise SystemExit("密码输入格式异常；未连接。")

remote_command = (
    "set -eu; "
    "test \"$(tr -d '\\000' </proc/device-tree/serial-number)\" = 8314b1e8ffd0321f; "
    "printf 'BOARD_B_SSH_PASSWORD_LOGIN_OK\\n'; id -u; pwd"
)

for host in sys.argv[1:]:
    ipaddress.ip_address(host)
    arguments = [
        "ssh", "-T", "-o", "BatchMode=no", "-o", "ConnectTimeout=6",
        "-o", "StrictHostKeyChecking=yes", "-o", "HostKeyAlgorithms=ssh-ed25519",
        "-o", "UserKnownHostsFile=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/board_b/20261102_deploy/known_hosts",
        "-o", "HostKeyAlias=192.168.100.123",
        "-o", "PreferredAuthentications=password", "-o", "PubkeyAuthentication=no",
        "-o", "KbdInteractiveAuthentication=no", "-o", "IdentityAgent=none",
        "-o", "NumberOfPasswordPrompts=1", f"root@{host}", remote_command,
    ]
    pid, terminal = pty.fork()
    if pid == 0:
        os.execvp(arguments[0], arguments)
    transcript = bytearray()
    sent = False
    finished = False
    try:
        deadline = time.monotonic() + 25
        while time.monotonic() < deadline:
            readable, _, _ = select.select([terminal], [], [], 0.2)
            if readable:
                try:
                    data = os.read(terminal, 8192)
                except OSError as error:
                    if error.errno == errno.EIO:
                        break
                    raise
                if not data:
                    break
                transcript.extend(data)
                if not sent and re.search(rb"(?i)password:\s*$", transcript):
                    if termios.tcgetattr(terminal)[3] & termios.ECHO:
                        raise RuntimeError("密码输入回显仍开启，停止本次核对。")
                    os.write(terminal, (secret + "\n").encode("utf-8"))
                    sent = True
            else:
                ended, status = os.waitpid(pid, os.WNOHANG)
                if ended:
                    finished = True
                    break
        else:
            raise TimeoutError("密码连接超时。")
        if not finished:
            _, status = os.waitpid(pid, 0)
            finished = True
        output = transcript.decode("utf-8", errors="replace").replace(secret, "[REDACTED]")
        exit_code = os.waitstatus_to_exitcode(status)
        print(f"[host] {host}\n{output}\n[SSH_EXIT] {exit_code}", flush=True)
        checks = [
            sent, exit_code == 0,
            "BOARD_B_SSH_PASSWORD_LOGIN_OK" in output,
            bool(re.search(r"(?m)^0\r?$", output)),
            bool(re.search(r"(?m)^/root\r?$", output)),
        ]
        if not all(checks):
            raise RuntimeError("本轮密码、板B身份、root权限或主目录核对未全部通过。")
        print("PASSWORD_ONLY_SSH_LOGIN=PASS", flush=True)
    finally:
        if not finished:
            os.kill(pid, signal.SIGKILL)
            os.waitpid(pid, 0)
        os.close(terminal)
