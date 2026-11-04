"""仅修新运行检查对内核线程瞬态调度状态的误判，保留原失败脚本。"""
from pathlib import Path
import difflib
import hashlib
import json

ROOT = Path(__file__).resolve().parents[3]
CALLS = ROOT / 'temp/sn100_host/root_manager_20261103'
OUT = Path(__file__).resolve().parent / 'runtime_batch10_01'
before = (CALLS / 'prepare_flat_batch10_01.sh').read_text()
old = '''        [[ ${later_fields[0]-} == "${fields[0]}" && ${later_fields[6]-} == "${fields[6]}" && ${later_fields[19]-} == "${fields[19]}" ]] || return 79
        return 1  # 僵尸/死亡或PF_KTHREAD内核线程没有存活用户exe。'''
new = '''        [[ ${later_fields[6]-} =~ ^[0-9]+$ && ${later_fields[19]-} == "${fields[19]}" ]] || return 79
        # 内核线程的S/R/D会随调度改变；两次同启动身份且PF_KTHREAD均置才跳过。
        if (( (10#${fields[6]} & 0x00200000) != 0 && (10#${later_fields[6]} & 0x00200000) != 0 )); then
            return 1
        fi
        # 死亡状态可在Z/X间变化；普通存活用户进程的exe不可读仍拒绝。
        if [[ ${fields[0]-} == Z || ${fields[0]-} == X ]] && [[ ${later_fields[0]-} == Z || ${later_fields[0]-} == X ]]; then
            return 1
        fi
        return 79'''
assert before.count(old) == 1
after = before.replace(old, new)
new_path = CALLS / 'prepare_flat_batch10_02.sh'
with new_path.open('x', encoding='utf-8', newline='\n') as stream:
    stream.write(after)
delta = ''.join(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
                                     fromfile='prepare_flat_batch10_01.sh', tofile='prepare_flat_batch10_02.sh'))
with (OUT / 'runtime_process_classification.diff').open('x', encoding='utf-8', newline='\n') as stream:
    stream.write(delta)
with (OUT / 'runtime_process_classification.json').open('x', encoding='utf-8', newline='\n') as stream:
    json.dump({'before_sha256': hashlib.sha256((CALLS / 'prepare_flat_batch10_01.sh').read_bytes()).hexdigest(),
               'after_sha256': hashlib.sha256(new_path.read_bytes()).hexdigest(),
               'board_executed': False, 'product_sources_changed': False,
               'diagnostic': 'temp/sn100_host/root_manager_20261103/diagnose_proc_identity_batch10_02.log'}, stream, indent=2)
    stream.write('\n')
print('RUNTIME_CHECK_REVISION_PREPARED=1; BOARD_EXECUTED=0')
