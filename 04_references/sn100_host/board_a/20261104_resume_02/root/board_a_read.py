"""只读核对板A身份、原配置指纹、进程和部署指向。"""
from pathlib import Path
import json
import subprocess
import sys

root = Path(__file__).resolve().parents[3]
work = Path(__file__).resolve().parent
known = root / '04_references/sn100_host/board_a/20261104_formal_merge_identity/known_hosts'
ssh = ['C:/Program Files/Git/usr/bin/ssh.exe', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=6',
       '-o', 'StrictHostKeyChecking=yes', '-o', 'HostKeyAlgorithms=ssh-ed25519',
       '-o', 'HostKeyAlias=rk3576-board-a', '-o', f'UserKnownHostsFile={known}',
       '-o', 'UpdateHostKeys=no', '-i', 'C:/Users/52400/.ssh/id_ed25519', 'root@192.168.100.123']
command = r'''set -eu
test "$(tr -d '\000' < /proc/device-tree/serial-number)" = 3bf8222fe1417f6a
printf 'SERIAL='; tr -d '\000' < /proc/device-tree/serial-number; printf '\nBOOT='; cat /proc/sys/kernel/random/boot_id
uname -r
base=/home/rkshare/programs/sn100_shell
printf 'CURRENT='; readlink -f "$base/current"
printf 'CONFIG_POINTERS\n'
for name in original_project_root instrument_configuration_file theme_configuration_file; do printf '%s=' "$name"; cat "$base/$name"; done
printf 'CONFIG_HASHES\n'
find "$base/runtime/data" -maxdepth 2 -type f \( -iname '*.ini' -o -iname '*.cfg' \) -print0 | sort -z | xargs -0 -r sha256sum
printf 'CONFIG_SAFE_VALUES\n'
sed -n '/^\[deviceInfo\]/,/^\[/p; /^\[instrument\]/,/^\[/p' "$base/runtime/data/config.ini" | head -30
printf 'PROCESSES\n'
ps -eo pid,lstart,args | awk '/sn100_shell|ACA_V1.0OffLineD-500/ && !/awk/ && !/sshd/ && !/bash -c/ && !/sh -c/ {print}'
printf 'CONTROLLER_LISTEN\n'; ss -H -ltnp 'sport = :5000'
printf 'LATEST_RUN\n'; ls -dt "$base"/runs/* | head -2
printf 'INPUT_DEVICES\n'; ls -l /dev/input/by-id/ 2>/dev/null || true
'''
name = sys.argv[1] if len(sys.argv) > 1 else 'identity_before'
result = subprocess.run(ssh + ['bash -s'], input=command.encode(), stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=35)
(work / f'{name}.stdout').write_bytes(result.stdout)
(work / f'{name}.stderr').write_bytes(result.stderr)
(work / f'{name}.exit').write_text(str(result.returncode) + '\n')
print(result.stdout.decode(errors='replace'))
print(result.stderr.decode(errors='replace'), file=sys.stderr)
sys.exit(result.returncode)
