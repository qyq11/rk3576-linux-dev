"""用户确认电脑重启后，保留真实中断记录，用同一已审源码新建完整构建。"""
from pathlib import Path
import ast
import hashlib
import json
here=Path(__file__).resolve().parent
root=here.parents[2]
task=root/'temp/sn100_host/workflow_night_20261102'
old=task/'integration_09_root_frozen_04'
new=task/'integration_09_root_frozen_05'
work=task/'integration_09_revision_04'
assert not (here/'build09_revision04_outer.exit').exists() and not (work/'build_exit.txt').exists() and not (work/'build_complete.json').exists()
assert not new.exists() and not (task/'integration_09_revision_05').exists()
def rec(p):return {'path':p.relative_to(root).as_posix(),'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'bytes':p.stat().st_size}
interruption=here/'build09_revision04_interruption.json'
state={'purpose':'用户确认刚才电脑关机并重启，Windows启动时间17:08:16；04最终退出未知，没有完整产物，不复用局部对象。',
       'windows_last_boot':'2026-11-03T17:08:16.5000000+08:00','source_change':False,'partial_objects_at_check':218,
       'user_requested_partial_cleanup':True,'cleanup_result':'automatic_approval_rejected_policy_blocked_files_still_present',
       'inventory':rec(here/'revision04_generated_inventory_before_cleanup.json'),
       'retained':[rec(p) for p in (here/'build09_revision04_outer.log',here/'build09_revision04_outer.started',work/'logs/build.log',work/'source_snapshot.json',work/'build_inputs.tsv')]}
with interruption.open('x',encoding='utf-8') as out:json.dump(state,out,ensure_ascii=False,indent=2);out.write('\n')
cfg=json.loads((old/'integration_09_tools_config.json').read_text(encoding='utf-8'))
new.mkdir();records=[]
for item in cfg['tools']:
    p=root/item['path'];assert rec(p)==item
    data=p.read_bytes()
    if p.name in ('build_integration_09.sh','integration_09_tool_support.py'):
        assert data.count(b'integration_09_revision_04')==1
        data=data.replace(b'integration_09_revision_04',b'integration_09_revision_05')
    dest=new/p.name;dest.write_bytes(data)
    if dest.suffix=='.py':ast.parse(data.decode('utf-8'))
    records.append(rec(dest))
cfg['tools']=records;cfg['evidence'].append(rec(interruption));cfg['evidence'].append(rec(here/'revision04_generated_inventory_before_cleanup.json'))
cfg['purpose']='第09批修订05：用户确认电脑重启导致04中断；沿用同一684源码/11片实际晋级链与批准审核，新目录从头完整编译，不复用02/03/04局部对象。'
with (new/'integration_09_tools_config.json').open('x',encoding='utf-8') as out:json.dump(cfg,out,ensure_ascii=False,indent=2);out.write('\n')
for stem in ('run_build09_revision04.py','run_postbuild09_revision04.py','run_checker09_revision04.py','prepare_activation09_revision04.py','prepare_live09_revision04.py','collect_live09_revision04.py','archive_live09_revision04.py'):
    data=(here/stem).read_text(encoding='utf-8').replace('root_frozen_04','root_frozen_05').replace('revision_04','revision_05').replace('revision04','revision05').replace('20261103_status_login_09_preparation_03','20261103_status_login_09_preparation_04')
    ast.parse(data)
    with (here/stem.replace('revision04','revision05')).open('x',encoding='utf-8',newline='\n') as out:out.write(data)
print(json.dumps({'config':rec(new/'integration_09_tools_config.json'),'tool_count':len(records),'source_review_unchanged':cfg['approved_final_review_sha256'],'partial_objects_reused':0},ensure_ascii=False))
