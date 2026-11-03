"""通过板B固定SSH入口执行一次只读结构查询，保留原输出与实际退出码。"""
from pathlib import Path
import subprocess

task = Path(__file__).resolve().parent
script = task / 'read_task_used_side_effects_board_b_01.sh'
output = task / 'task_reagent_side_effects_20261103'
output.mkdir()
command = ['ssh', '-o', 'BatchMode=yes', '-o', 'StrictHostKeyChecking=yes',
           '-o', 'HostKeyAlias=rk3576-board-b', '-o',
           'UserKnownHostsFile=C:/Users/52400/.ssh/known_hosts_rk3576_board_b',
           '-o', 'ConnectTimeout=8', 'root@192.168.100.123', 'bash', '-s']
with (output / 'execution.log').open('xb') as stream:
    result = subprocess.run(command, input=script.read_bytes(), stdout=stream, stderr=subprocess.STDOUT)
with (output / 'execution.exit').open('x', encoding='ascii') as stream:
    stream.write(str(result.returncode) + '\n')
print('READ_ONLY_TARGET_SIDE_EFFECTS_ACTUAL_EXIT=' + str(result.returncode))
raise SystemExit(result.returncode)
