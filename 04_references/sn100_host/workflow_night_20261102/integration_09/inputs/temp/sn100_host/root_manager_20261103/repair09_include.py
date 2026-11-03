"""保留第09批失败现场，只追加当前告警页所需的完整类型包含。"""
from pathlib import Path
import difflib
import hashlib
import json
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[3]
TASK=ROOT/'temp/sn100_host/workflow_night_20261102'
PARENT=TASK/'integration_09_review_01'
WORK=TASK/'integration_09_revision_03_review'
PROGRAM=ROOT/'06_programs/sn100_host'
PREFIX='06_programs/sn100_host/'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(p,v):
    with p.open('x',encoding='utf-8',newline='\n') as f:json.dump(v,f,ensure_ascii=False,indent=2);f.write('\n')
def rec(p):return {'path':p.relative_to(ROOT).as_posix(),'sha256':sha(p),'bytes':p.stat().st_size}
def command(args,cwd,name):
    r=subprocess.run(args,cwd=cwd,capture_output=True)
    (WORK/(name+'.log')).write_bytes(r.stdout+r.stderr)
    (WORK/(name+'.exit')).write_text(str(r.returncode)+'\n',encoding='ascii')
    assert r.returncode==0,name
parent=json.loads((PARENT/'final_merge_review.json').read_text(encoding='utf-8'))
inherited=json.loads((PARENT/'promotion_progress.json').read_text(encoding='utf-8'))
before=parent['final_source_files']
assert inherited['complete'] and inherited['applied']==[{k:x[k] for k in ('index','patch','sha256')} for x in parent['patches']]
assert all(sha(PROGRAM/n)==h for n,h in before.items())
assert (TASK/'integration_09_revision_02/build_exit.txt').read_text().strip()=='1'
assert (ROOT/'temp/sn100_host/root_manager_20261103/build09_outer.exit').read_text().strip()=='1'
assert not WORK.exists();WORK.mkdir()
stage=WORK/'preflight'
for n in before:
    p=stage/PREFIX/n;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(PROGRAM/n,p)
shutil.copyfile(ROOT/'.gitattributes',stage/'.gitattributes')
command(['git','init','--initial-branch=codex/integration09-include-fix'],stage,'stage_init')
name='alarm_history/alarm_history_page_current.cpp'
old=(PROGRAM/name).read_bytes();eol=b'\r\n' if b'\r\n' in old else b'\n'
needle=b'#include "ui/cusdialogmask.h"'+eol
assert old.count(needle)==1
new=old.replace(needle,needle+b'#include "ui/cusmessagebox.h"'+eol)
patch=WORK/'alarm_current_complete_type.patch'
patch.write_bytes(''.join(difflib.unified_diff(old.decode('utf-8').splitlines(True),new.decode('utf-8').splitlines(True),fromfile='a/'+PREFIX+name,tofile='b/'+PREFIX+name)).encode('utf-8'))
save(WORK/'11_before_source_manifest.json',before)
command(['git','-c','core.autocrlf=false','apply','--check',str(patch)],stage,'preflight_11_check')
command(['git','-c','core.autocrlf=false','apply',str(patch)],stage,'preflight_11_apply')
after={n:sha(stage/PREFIX/n) for n in before}
assert {n for n in before if before[n]!=after[n]}=={name}
save(WORK/'11_after_source_manifest.json',after)
item={'index':11,'patch':patch.relative_to(ROOT).as_posix(),'sha256':sha(patch),
      'before_checkpoint':rec(WORK/'11_before_source_manifest.json'),'after_checkpoint':rec(WORK/'11_after_source_manifest.json')}
review={**parent,'format':'sn100-integration-09-final-merge-revision-03-v1','patches':parent['patches']+[item],
 'final_source_files':after,'inherited_final_review':rec(PARENT/'final_merge_review.json'),
 'inherited_promotion_progress':rec(PARENT/'promotion_progress.json'),
 'inherited_patch_indexes':list(range(1,11)),'applied_in_this_execution':[11],
 'scope':'继承已实际合入的前10片，只追加第11片完整类型包含；重新完整交叉编译，原失败对象不复用。'}
review['historical_failed_checks']=parent['historical_failed_checks']+[rec(p) for p in [
 TASK/'integration_09_revision_02/logs/build.log',TASK/'integration_09_revision_02/build_exit.txt',
 ROOT/'temp/sn100_host/root_manager_20261103/build09_outer.log',ROOT/'temp/sn100_host/root_manager_20261103/build09_outer.exit']]
review_path=WORK/'final_merge_review.json';save(review_path,review)
save(WORK/'source_manifest_after.json',after)
progress={**inherited,'format':'sn100-integration-09-promotion-revision-03-v1','complete':False,'phase':'applying','current_patch':11,
 'final_source_files':after,'patches':review['patches'],'final_merge_review_sha256':sha(review_path),'merge_review_sha256':sha(review_path),
 'promotion_script_sha256':sha(Path(__file__)),'inherited_applied':inherited['applied'],'applied_in_this_execution':[],
 'inherited_promotion_progress':rec(PARENT/'promotion_progress.json')}
path=WORK/'promotion_progress.json'
path.write_text(json.dumps(progress,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
command(['git','-c','core.autocrlf=false','apply','--check',str(patch)],ROOT,'formal_11_check')
command(['git','-c','core.autocrlf=false','apply',str(patch)],ROOT,'formal_11_apply')
assert all(sha(PROGRAM/n)==h for n,h in after.items())
progress.update(complete=True,phase='complete',current_patch=None,verified_source_paths=len(after),
 applied=inherited['applied']+[{k:item[k] for k in ('index','patch','sha256')}],preflight_applied=list(range(1,12)),applied_in_this_execution=[11])
path.write_text(json.dumps(progress,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('INCLUDE_FIX_FORMALLY_APPLIED=1; INHERITED_PATCHES=10; NEW_PATCHES=1; SOURCE_COUNT='+str(len(after)))
