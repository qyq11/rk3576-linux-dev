"""仅收尾本轮归档及来源校验；不执行产品或连接板卡。"""
from pathlib import Path
import hashlib,json
ROOT=Path(__file__).resolve().parents[4]
HERE=Path(__file__).resolve().parent
OUT=ROOT/'04_references/sn100_host/board_a/20261104_sample_add_order'
BACKUP=ROOT/'07_backups/sn100_host/board_a/sample_order_20261104_01'
def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()
def copy(source,target):
    assert not target.exists();target.parent.mkdir(parents=True,exist_ok=True)
    with target.open('xb') as f:f.write(source.read_bytes())
    assert sha(source)==sha(target)
def seal(directory):
    sums=directory/'SHA256SUMS'
    if sums.exists():copy(sums,HERE/(directory.name+'_before_final_SHA256SUMS.txt'))
    paths=sorted(p for p in directory.rglob('*') if p.is_file() and p!=sums)
    assert all(not p.is_symlink() for p in paths)
    sums.write_text(''.join(sha(p)+'  '+p.relative_to(directory).as_posix()+'\n' for p in paths),encoding='utf-8')
    for row in sums.read_text().splitlines():
        value,name=row.split('  ',1);assert sha(directory/name)==value
    return {'files':len(paths),'checksum_sha256':sha(sums)}
assert (HERE/'keep_running_handoff_02.exit').read_text().strip()=='0'
for suffix in ('.sh','.command.json','.stdout.log','.stderr.log','.exit'):
    copy(HERE/('keep_running_handoff_02'+suffix),OUT/'execution_originals'/('keep_running_handoff_02'+suffix))
copy(HERE/'seal_evidence_01.py',OUT/'execution_originals/seal_evidence_01.py')
copy(HERE/'final_archive_check_01.py',OUT/'execution_originals/final_archive_check_01.py')
copy(ROOT/'01_docs/plans/sn100_next_session_prompt_20261103.md',OUT/'next_session_prompt_at_pause.md')
for name in ('prepare_board_a_checker_01.py','run_board_a_checker_build_01.py'):
    copy(ROOT/'temp/sn100_host/resume_dispatch_20261103'/name,OUT/'build_binding'/name)
source=ROOT/'temp/sn100_host/workflow_next_20261103/integration_15_source_acceptance_01/sample_empty_backend/source_after.json'
assert sha(source)=='f3519bacb0efca4077ef93ef21f59e906021585dbd9ae414a37e7bb9b2abf6d0'
sources=json.loads(source.read_bytes());assert len(sources)==697
for name,value in sources.items():assert sha(ROOT/'06_programs/sn100_host'/name)==value
assert sha(ROOT/'.gitattributes')=='c46d8032dc7311b5911dfb7e578ad70ba1258ac924b72675f8b2192c5b9d445a'
summary=json.loads((OUT/'summary.json').read_bytes())
assert summary['software_checks_passed']==9 and summary['software_checks_failed']==1 and summary['application_exit'] is None
report={'formal_sources':len(sources),'formal_sources_unchanged':True,'board':'board_a','all_sn100_tasks_paused':True,
    'evidence':seal(OUT),'application_restore':seal(BACKUP/'application'),
    'controller_original_inputs':seal(BACKUP/'controller_inputs'),'previous_theme':seal(BACKUP/'previous_theme'),
    'product_executed_on_computer':False,'board_contacted_by_this_check':False}
with (HERE/'final_archive_verification_01.json').open('x',encoding='utf-8') as f:json.dump(report,f,ensure_ascii=False,indent=2);f.write('\n')
print(json.dumps(report,ensure_ascii=False))
