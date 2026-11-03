"""保留本次实际派发退出，再只读收集，并单独执行原RX独立核对。"""
from pathlib import Path
import json
import re
import subprocess
import sys
here=Path(__file__).resolve().parent
root=here.parents[2]
candidate=root/'temp/sn100_host/board_b/20261103_status_login_09_preparation_04'
live=root/'temp/sn100_host/workflow_night_20261102/integration_09_live'
inputs=json.loads((here/'live09_inputs.json').read_text())
log=(here/'run09_revision05.log').read_bytes()
code=int((here/'run09_revision05.exit').read_text())
assert code in (0,1), '网络或前置失败需先独立保存和诊断，不当作case已结束'
assert ('RUN_DIRECTORY='+inputs['remote_case']).encode() in log
assert re.search(rb'(?m)^WRAPPER_EXIT=[0-9]+\r?$',log)
live.mkdir(exist_ok=True)
with (live/'auto_dispatch.log').open('xb') as out:out.write(log)
with (live/'auto_dispatch.exit').open('x') as out:out.write(str(code)+'\n')
def wsl(p):return '/mnt/c/'+p.relative_to(Path('C:/')).as_posix()
def execute(stem,args):
    assert not (here/(stem+'.log')).exists() and not (here/(stem+'.exit')).exists()
    with (here/(stem+'.log')).open('xb') as out:
        result=subprocess.run(['wsl','-d','Ubuntu-24.04','-u','qyq','--','python3',*args],stdout=out,stderr=subprocess.STDOUT)
    (here/(stem+'.exit')).write_text(str(result.returncode)+'\n')
    print(stem+'_ACTUAL_EXIT='+str(result.returncode),flush=True)
    return result.returncode
collected=execute('collect_live09_revision05',[wsl(candidate/'collect_case_09.py'),'--execute','--remote-case',inputs['remote_case'],
       '--expected-dispatch-exit',str(code),'--expected-case-exit',str(code)])
if collected:sys.exit(collected)
case=live/inputs['remote_case'].split('/')[-1]
review=here/'independent_review09_revision05.json'
assert not review.exists()
independent=execute('independent_review09_revision05',[wsl(candidate/'review_actual_ad_09.py'),wsl(case),wsl(candidate),'--output',wsl(review)])
print('COLLECTOR_EXIT_AND_INDEPENDENT_EXIT_SEPARATELY_PRESERVED=1',flush=True)
sys.exit(independent)
