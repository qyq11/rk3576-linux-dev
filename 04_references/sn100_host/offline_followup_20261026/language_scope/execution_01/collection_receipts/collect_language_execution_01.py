#!/usr/bin/env python3
"""Record the actual metadata collector exit and finalize only its new text archive."""
import datetime
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time
import traceback


ROOT = Path(__file__).resolve().parents[4]
WORK = ROOT / 'temp/sn100_host/qc_history_integration_20261026'
BASE = WORK / 'integration_drivers'
OUT = ROOT / '04_references/sn100_host/offline_followup_20261026/language_scope/execution_01'


def check(value, message):
    if not value:
        raise RuntimeError(message)


def sha(value):
    return hashlib.sha256(value).hexdigest()


def raw(path):
    check(path.is_file() and not path.is_symlink() and path.resolve().is_relative_to(ROOT), str(path))
    value = path.read_bytes();check(b'\x00' not in value, 'NUL requires review: ' + str(path))
    value.decode('utf-8',errors='strict');return value


def read(path):
    return json.loads(raw(path))


def save(path,value,exclusive=True):
    with path.open('xb' if exclusive else 'wb') as handle:
        handle.write((json.dumps(value,ensure_ascii=False,indent=2)+'\n').encode('utf-8'))


def main():
    stem = BASE/'language_archive_collection_01'
    argv = [sys.executable,'-B',str(BASE/'archive_language_execution.py'),'--root-go','--attempt','language_archive_01']
    started = datetime.datetime.now(datetime.timezone.utc).isoformat();mono=time.monotonic_ns()
    save(Path(str(stem)+'.command.json'),{'argv':argv,'cwd':str(ROOT),'started_utc':started,'scope':'metadata collector only; no language/build/test controller run'})
    with Path(str(stem)+'.stdout.log').open('xb') as stdout,Path(str(stem)+'.stderr.log').open('xb') as stderr:
        process = subprocess.Popen(argv,cwd=ROOT,stdout=stdout,stderr=stderr)
        save(Path(str(stem)+'.process.json'),{'pid':process.pid,'started_utc':started})
        timed=False
        try:code=process.wait(timeout=90)
        except subprocess.TimeoutExpired:
            timed=True;process.kill();code=process.wait(timeout=5)
    # Persist actual return before checking it or touching final metadata.
    receipt={'exit_code':code,'timed_out':timed,'pid':process.pid,'source':'actual subprocess.Popen returncode',
             'finished_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'elapsed_ns':time.monotonic_ns()-mono}
    save(Path(str(stem)+'.exit.json'),receipt)
    check(code==0 and not timed,'Metadata collector failed; preserve attempt and use fresh retry')
    check(not (OUT/'INCOMPLETE.json').exists(),'Incomplete collection')
    copied=read(OUT/'copied_inputs.json')
    for name,item in copied.items():
        check(len(raw(OUT/name))==item['bytes'] and sha(raw(OUT/name))==item['sha256'],'Copied original changed')
        check(len(raw(ROOT/item['source']))==item['bytes'] and sha(raw(ROOT/item['source']))==item['sha256'],'Original input changed')
    dest=OUT/'collection_receipts';dest.mkdir()
    selected=[Path(str(stem)+s) for s in ('.command.json','.process.json','.exit.json','.stdout.log','.stderr.log')]
    selected += [WORK/'archive_attempts/language_archive_01/result.json',Path(__file__).resolve()]
    extra={}
    for source in selected:
        value=raw(source);target=dest/source.name
        with target.open('xb') as handle:handle.write(value)
        check(raw(target)==value,'Receipt copy differs')
        extra[target.relative_to(OUT).as_posix()]={'source':source.relative_to(ROOT).as_posix(),'bytes':len(value),'sha256':sha(value)}
    git_argv=['git','check-ignore','-v','--',OUT.relative_to(ROOT).as_posix()+'/handoff.json']
    save(dest/'git_ignore.command.json',{'argv':git_argv,'cwd':str(ROOT)})
    git=subprocess.run(git_argv,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=15)
    save(dest/'git_ignore.exit.json',{'exit_code':git.returncode,'expected_exit_for_not_ignored':1})
    (dest/'git_ignore.stdout.log').write_bytes(git.stdout);(dest/'git_ignore.stderr.log').write_bytes(git.stderr)
    raw(dest/'git_ignore.stdout.log');raw(dest/'git_ignore.stderr.log')
    check(git.returncode==1 and not git.stdout and not git.stderr,'Archive ignored or git failure')
    save(OUT/'collection_receipt_inputs.json',extra)
    proof=read(OUT/'archive_verification.json')
    proof['actual_metadata_collector_exit']=receipt
    proof['checks']['actual_metadata_collector_exit_zero']=True
    proof['checks']['git_not_ignored_actual_exit_one']=True
    save(OUT/'archive_verification.json',proof,False)
    handoff=read(OUT/'handoff.json')
    handoff['actual_metadata_collector_exit']=0
    handoff['actual_metadata_collector_receipt']='collection_receipts/language_archive_collection_01.exit.json'
    handoff['git_ignore_actual_exit']=1
    save(OUT/'handoff.json',handoff,False)
    paths=sorted(x for x in OUT.rglob('*') if x.is_file() and x!=OUT/'SHA256SUMS')
    check(all(not any(p in ('build','build_probe','build_test','runtime','test_data','tmp','.obj','.moc') for p in x.relative_to(OUT).parts) for x in paths),'Forbidden copied namespace')
    hashes={x.relative_to(OUT).as_posix():{'bytes':len(raw(x)),'sha256':sha(raw(x))} for x in paths}
    (OUT/'SHA256SUMS').write_text(''.join(v['sha256']+'  '+name+'\n' for name,v in hashes.items()),encoding='utf-8',newline='\n')
    check(all(len(raw(OUT/name))==v['bytes'] and sha(raw(OUT/name))==v['sha256'] for name,v in hashes.items()),'Final archive readback differs')
    for name,item in copied.items():check(sha(raw(ROOT/item['source']))==item['sha256'],'Original changed at finalization')
    old=OUT.parent
    check(all(len(raw(old/name))==v['bytes'] and sha(raw(old/name))==v['sha256'] for name,v in proof['old_scope_protected_metadata'].items()),'Old scope metadata changed')
    result={'passed':True,'files':len(hashes)+1,'sha_entries':len(hashes),
            'copied_original_text_files':len(copied),'collector_actual_exit':code,'parent_reported_actual_exit':0,
            'sha256sums_sha256':sha(raw(OUT/'SHA256SUMS')),'handoff_sha256':sha(raw(OUT/'handoff.json')),
            'archive_verification_sha256':sha(raw(OUT/'archive_verification.json')),
            'execution_sha256':sha(raw(OUT/'execution.json')),'copied_inputs_sha256':sha(raw(OUT/'copied_inputs.json'))}
    save(WORK/'archive_attempts/language_archive_01/final_archive_readback.json',result)
    print(json.dumps(result,ensure_ascii=False))


if __name__=='__main__':
    try:
        main()
    except Exception as error:
        attempt=WORK/'archive_attempts/language_archive_01'
        failure={'passed':False,'error':str(error),'traceback':traceback.format_exc(),'no_business_stage_rerun':True}
        if attempt.is_dir() and not (attempt/'finalization_failure.json').exists():
            save(attempt/'finalization_failure.json',failure)
        receipt_path=BASE/'language_archive_collection_01.exit.json'
        if receipt_path.is_file() and read(receipt_path)['exit_code']==0 and OUT.is_dir() and not (OUT/'INCOMPLETE.json').exists():
            save(OUT/'INCOMPLETE.json',failure)
        raise
