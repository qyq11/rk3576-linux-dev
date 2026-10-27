"""Metadata/text collector only. Never opens an executable, object or runtime DB."""
import base64
import hashlib
import json
from pathlib import Path
import sys

HERE=Path(__file__).resolve().parent
WORK=HERE.parent
ROOT=WORK.parents[2]
DEST=ROOT/'04_references/sn100_host/qc_history_integration_20261026/windows'
STAGE=WORK/'windows_archive_preparation'
ALLOW_WRAPPERS=STAGE/'audited_byte_logs.json'

def digest(data):return hashlib.sha256(data).hexdigest()
def load(path):return json.loads(path.read_text(encoding='utf-8-sig'))
def save(path,value):
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

def candidates():
    rows={}
    def add(path,target):
        assert path.is_file(),path
        assert path.suffix.lower() not in ('.exe','.o','.dll','.db','.sqlite','.ini','.a','.pdb')
        key=target.as_posix()
        assert key not in rows or rows[key]==path,(key,path)
        rows[key]=path
    for name in ('windows','windows_02','windows_03','windows_04'):
        directory=WORK/'platforms'/name
        for path in sorted(directory.iterdir()):
            if path.is_file() and path.suffix.lower() in ('.json','.log'):
                add(path,Path('attempts')/name/path.name)
        for role in ('shell','renderer'):
            build=directory/(role+'_build')
            for name2 in ('Makefile','object_script.sn100_shell','object_script.sn100_report_renderer'):
                path=build/name2
                if path.is_file():add(path,Path('attempts')/name/(role+'_build')/name2)
    manifest=load(HERE/'platform_preparation.json')
    for row in manifest['controller_inputs']:
        add(WORK/row['path'],Path('final_controller_inputs')/row['path'])
    for name in ('platform_preparation.json','RUN_PLATFORM_COMMANDS.txt'):
        add(HERE/name,Path('final_controller_inputs')/name)
    reconstructed=STAGE/'reconstructed_run02/native_job.cs'
    if reconstructed.is_file():add(reconstructed,Path('controller_history/reconstructed_run02/native_job.cs'))
    add(Path(__file__).resolve(),Path('collector/archive_windows_metadata.py'))
    finalizer=HERE/'finalize_windows_metadata_archive.py'
    if finalizer.is_file():add(finalizer,Path('collector/finalize_windows_metadata_archive.py'))
    for folder in ('audit_originals','observations','probes'):
        for path in sorted((HERE/folder).rglob('*')):
            if path.is_file() and path.suffix.lower() in ('.json','.log','.raw','.ps1','.py','.cs','.txt'):
                add(path,Path('controller_history')/folder/path.relative_to(HERE/folder))
    for directory in sorted(HERE.glob('platform_static_review*')):
        for path in sorted(directory.rglob('*')):
            if path.is_file() and path.suffix.lower() in ('.json','.log'):
                add(path,Path('controller_history')/directory.name/path.relative_to(directory))
    for name in ('final_before.json','final_after.json','verification.json'):
        path=WORK/'native'/name
        if path.is_file():add(path,Path('native_binding')/name)
    old=ROOT/'temp/sn100_host/qc_local_20261025'
    for role in ('shell','renderer'):
        directory=old/'platforms/windows'
        for suffix in ('make.log','compiled_commands.json','object_rules.json','link_source_map.json'):
            path=directory/(role+'_'+suffix)
            if path.is_file():add(path,Path('historical426_parser_inputs')/path.name)
        for name in ('Makefile','object_script.sn100_shell','object_script.sn100_report_renderer'):
            path=directory/(role+'_build')/name
            if path.is_file():add(path,Path('historical426_parser_inputs')/(role+'_build')/name)
    for row in manifest['prior_passed_receipts']+manifest['prior_controller_inputs']:
        path=ROOT/row['path']
        assert digest(path.read_bytes())==row['sha256'],path
        add(path,Path('historical426_provenance')/Path(row['path']).relative_to('temp/sn100_host/qc_local_20261025'))
    return rows

def safe_text(data):
    try:text=data.decode('utf-8-sig')
    except UnicodeDecodeError:return False,'invalid_utf8'
    if any(ord(char)<32 and char not in '\t\r\n' for char in text):return False,'NUL_or_control_character'
    return True,None

def inventory():
    assert not DEST.exists(),'Existing archive target must not be overwritten'
    STAGE.mkdir(exist_ok=True)
    rows=[]
    unsafe=[]
    for target,path in candidates().items():
        data=path.read_bytes()
        ok,reason=safe_text(data)
        row={'source':str(path),'target':target,'bytes':len(data),'sha256':digest(data),'text_safe':ok,'reason':reason}
        rows.append(row)
        if not ok:unsafe.append(row)
    save(STAGE/'candidate_inputs.json',rows)
    save(STAGE/'unsafe_inputs.json',unsafe)
    print(json.dumps({'files':len(rows),'unsafe':unsafe},ensure_ascii=False,indent=2))

def collect():
    assert not DEST.exists(),'Fresh archive target required'
    rows=load(STAGE/'candidate_inputs.json')
    audited=load(ALLOW_WRAPPERS)
    wrappers={row['source']:row for row in audited}
    DEST.mkdir(parents=True)
    copied=[]
    excluded=[]
    for row in rows:
        source=Path(row['source']);data=source.read_bytes()
        assert len(data)==row['bytes'] and digest(data)==row['sha256'],source
        ok,reason=safe_text(data)
        target=DEST/row['target']
        assert target.is_relative_to(DEST)
        if ok:
            target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
            assert target.read_bytes()==data
            copied.append({**row,'archive_target':target.relative_to(DEST).as_posix(),'representation':'original_UTF8_text_bytes'})
        elif str(source) in wrappers:
            approval=wrappers[str(source)]
            assert approval['sha256']==row['sha256'] and approval['bytes']==row['bytes']
            wrapper=target.with_name(target.name+'.captured_bytes.json')
            value={'schema':'sn100-captured-log-bytes-v1','source':str(source),'original_bytes':len(data),
                'original_sha256':digest(data),'representation':'lossless_base64_of_captured_bytes',
                'not_a_unicode_transcript':True,'not_an_exit_receipt':True,'encoding_note':approval['encoding_note'],
                'full_content_review':approval['full_content_review'],'base64':base64.b64encode(data).decode('ascii')}
            save(wrapper,value)
            reread=load(wrapper)
            assert base64.b64decode(reread['base64'])==data
            copied.append({**row,'archive_target':wrapper.relative_to(DEST).as_posix(),'representation':'lossless_captured_bytes_wrapper'})
        else:
            excluded.append({**row,'reason':'Unsafe ordinary text is not copied without explicit per-file byte-wrapper authorization'})
    save(DEST/'archive_mapping.json',{'copied':copied,'excluded':excluded,
        'no_executables_objects_runtime_databases_INI_or_authentication_copied':True,
        'no_object_or_executable_fingerprints_recomputed':True})
    print(json.dumps({'copied':len(copied),'excluded':excluded,'destination':str(DEST)},ensure_ascii=False,indent=2))

if __name__=='__main__':
    assert sys.argv[1:] in (['inventory'],['collect'])
    inventory() if sys.argv[1]=='inventory' else collect()
