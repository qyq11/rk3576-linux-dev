"""只核对并归档本次真实结果；不连接板卡、不继续功能测试。"""
from pathlib import Path
import base64,hashlib,json,re,shutil,struct,zlib
ROOT=Path(__file__).resolve().parents[4]
HERE=Path(__file__).resolve().parent
OUT=ROOT/'04_references/sn100_host/board_a/20261104_sample_add_order'
APP=ROOT/'07_backups/sn100_host/board_a/sample_order_20261104_01/application'
RESUME=ROOT/'temp/sn100_host/resume_dispatch_20261103'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def copy(p,destination):
    assert p.is_file() and not p.is_symlink()
    destination.parent.mkdir(parents=True,exist_ok=True)
    with destination.open('xb') as f:f.write(p.read_bytes())
    assert sha(p)==sha(destination)
def png(raw):
    assert raw.startswith(b'\x89PNG\r\n\x1a\n')
    offset=8;types=[]
    while offset<len(raw):
        count=int.from_bytes(raw[offset:offset+4],'big');kind=raw[offset+4:offset+8]
        chunk=raw[offset+4:offset+8+count];crc=int.from_bytes(raw[offset+8+count:offset+12+count],'big')
        assert zlib.crc32(chunk)&0xffffffff==crc
        types.append(kind);offset+=12+count
    assert offset==len(raw) and types[0]==b'IHDR' and types[-1]==b'IEND' and b'IDAT' in types
def main():
    assert (HERE/'collect_final_01.exit').read_text().strip()=='0'
    raw=(HERE/'collect_final_01.stdout.log').read_text(encoding='utf-8')
    before=raw.split('IDENTITY_BEFORE_BEGIN\n')[1].split('IDENTITY_BEFORE_END\n')[0]
    after=raw.split('IDENTITY_AFTER_BEGIN\n')[1].split('IDENTITY_AFTER_END\n')[0]
    assert before==after and 'APP_PID=5836\nAPP_TICKS=225261\nAPP_UID=1000\n' in before
    assert 'APPLICATION_EXIT=null\nAPPLICATION_KEEP_RUNNING=1\nNO_STOP_OR_RETRY_REQUESTED=1\n' in raw
    OUT.mkdir(parents=True)
    captures={}
    for name in ('result.json','check_exit.txt','first_new_empty_rack.png','second_new_empty_rack.png'):
        block=raw.split('CAPTURE_FILE_BEGIN='+name+'\n')[1].split('CAPTURE_FILE_END='+name+'\n')[0]
        lines=block.splitlines();digest=lines[0].split()[0]
        data=base64.b64decode(next(line[7:] for line in lines if line.startswith('BASE64=')),validate=True)
        assert hashlib.sha256(data).hexdigest()==digest
        with (OUT/name).open('xb') as f:f.write(data)
        captures[name]={'sha256':digest,'bytes':len(data)}
        if name.endswith('.png'):png(data)
    result=json.loads((OUT/'result.json').read_bytes())
    assert result['schema']=='sn100-sample-add-order-review-v1' and result['board']=='board_a'
    assert result['release_id']=='d4501a69e100adbd3e3b87a4f311533ed7cfdabdd69d44f124fd822a7add4e4d'
    checks=result['checks']
    assert len(checks)==10 and all(row['passed'] is True for row in checks[:9]) and checks[-1]['passed'] is False
    assert result['failed']==sum(not row['passed'] for row in checks)==1 and result['completed'] is False
    assert result['check_exit']==int((OUT/'check_exit.txt').read_text())==1
    assert checks[-1]['name']==result['reason'] and result['last_step']=='choose_other_container_as_unsaved_draft'
    assert result['application_close_requested_by_checker'] is False and result['application_exit_verified'] is False
    assert result['configuration_unchanged'] is True and result['watched_task_or_motion_tx_observed'] is False
    assert result['created_racks']==[{'id':2,'name':'S01','still_in_snapshot':True},{'id':3,'name':'S02','still_in_snapshot':True}]
    assert result['observed_tx_types']==['Qj']
    complete='SAMPLE_ADD_REVIEW_COMPLETE check_exit= 1 saved= true exit_saved= true'
    assert raw.count(complete)==1
    originals=OUT/'execution_originals'
    originals.mkdir()
    for p in sorted(HERE.iterdir()):
        if p.is_file() and p.name!='known_hosts.board_a' and p.name!='seal_evidence_01.py':copy(p,originals/p.name)
    for p in (RESUME/'root_sample_checker_board_a_review_01/sample_add_review.cpp',
        RESUME/'sample_checker15_board_a_wrapper_01/prepare_and_build.py',
        RESUME/'sample_checker15_board_a_wrapper_01/preparation.json',
        RESUME/'sample_checker15_board_a_exec_01_01/actual_compile_link_commands.json',
        RESUME/'sample_checker15_board_a_exec_01_01/build_01/binding.json'):
        copy(p,OUT/'build_binding'/p.name)
    for suffix in ('.command.json','.stdout.log','.stderr.log','.exit'):
        p=RESUME/('board_a_checker_build_01'+suffix);copy(p,OUT/'build_binding'/p.name)
    sourcesdir=APP/'checker_sources';sourcesdir.mkdir()
    for name in ('sample_add_review.cpp','bind_sample_add_build.py','build_sample_add_review.sh'):
        copy(RESUME/'root_sample_checker_board_a_review_01'/name,sourcesdir/name)
    for name in ('prepare_and_build.py','preparation.json'):
        copy(RESUME/'sample_checker15_board_a_wrapper_01'/name,sourcesdir/name)
    config=raw.split('CONFIGURATION_FINAL_BEGIN\n')[1].split('CONFIGURATION_FINAL_END\n')[0]
    initial=(HERE/'prepare_configuration_01.stdout.log').read_text()
    assert initial.startswith(config)
    (OUT/'configuration_and_pointers.sha256').write_text(config,encoding='utf-8')
    summary={'board':'board_a','serial':'3bf8222fe1417f6a','boot':'559493ce-5319-42b6-9383-bf7b93764a35',
        'parent_formal_release':result['release_id'],'variant_release':'0b0b93787b055f51bba1ac579e0a79488959940284e0a702ab2fa06a1934e743',
        'application_pid':5836,'application_start_ticks':225261,'application_uid':1000,
        'application_exit':None,'application_kept_running':True,'controller_pid':4426,'controller_start_ticks':196590,
        'checker_exit':1,'software_checks_passed':9,'software_checks_failed':1,'completed':False,'reason':result['reason'],
        'unfinished':['未保存草稿后新增并取消','放弃自建草稿','第三架及本轮空架清理','实体输入','实屏视觉','真实仪器检测','资源门槛和长稳'],
        'created_empty_racks_preserved':result['created_racks'],'configuration_and_pointers_unchanged_during_review':True,
        'observed_tx_types':['Qj'],'same_process_before_after':True,'captures':captures,'all_sn100_tasks_paused_after_this_review':True}
    (OUT/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (OUT/'SHA256SUMS').write_text(''.join(sha(p)+'  '+p.relative_to(OUT).as_posix()+'\n' for p in sorted(OUT.rglob('*')) if p.is_file()),encoding='utf-8')
    (APP/'SHA256SUMS').write_text(''.join(sha(p)+'  '+p.relative_to(APP).as_posix()+'\n' for p in sorted(APP.rglob('*')) if p.is_file() and p.name!='SHA256SUMS'),encoding='utf-8')
    print(json.dumps(summary,ensure_ascii=False))
if __name__=='__main__':main()
