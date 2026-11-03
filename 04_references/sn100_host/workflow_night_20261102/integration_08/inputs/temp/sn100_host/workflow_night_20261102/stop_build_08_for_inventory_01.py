"""确认是本次08构建进程组后停止；保留目录、日志和真实退出，不自动重跑。"""
from pathlib import Path
import subprocess

task = Path(__file__).resolve().parent
root = task.parents[2]
work = task / 'integration_08_build_dispatch'
code = r'''
from pathlib import Path
import json, os, signal, sys
pid = 307
expected = ['/bin/bash', 'temp/sn100_host/workflow_night_20261102/integration_08_tool_candidate/build_integration_08.sh']
def args(p):
    return Path('/proc/%d/cmdline' % p).read_bytes().rstrip(b'\0').decode().split('\0')
assert args(pid) == expected, 'not this exact build command'
assert os.getpgid(pid) == pid and os.getpgrp() != pid
assert os.readlink('/proc/%d/cwd' % pid) == sys.argv[1]
members = []
for p in Path('/proc').iterdir():
    if not p.name.isdecimal(): continue
    try:
        n = int(p.name)
        if os.getpgid(n) == pid: members.append({'pid': n, 'args': args(n)})
    except (FileNotFoundError, ProcessLookupError): pass
assert members and args(pid) == expected
print(json.dumps({'verified_process_group': pid, 'members': members}), flush=True)
os.killpg(pid, signal.SIGTERM)
print('BUILD_PROCESS_GROUP_TERMINATED_FOR_INPUT_INVENTORY=1', flush=True)
'''
linux_root = '/mnt/c/' + root.as_posix()[3:]
command = ['wsl.exe', '-d', 'Ubuntu-24.04', '-u', 'qyq', '--', '/usr/bin/python3', '-c', code, linux_root]
with (work / 'inventory_stop.log').open('xb') as output:
    result = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT)
with (work / 'inventory_stop.exit').open('x', encoding='ascii') as output:
    output.write(str(result.returncode) + '\n')
print('VERIFIED_BUILD_STOP_SIGNAL_ACTUAL_EXIT=' + str(result.returncode))
raise SystemExit(result.returncode)
