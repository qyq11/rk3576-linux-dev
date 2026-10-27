#!/usr/bin/env python3
"""Attach actual collector receipts; reread the new metadata-only post-platform archive."""
import hashlib
import json
from pathlib import Path
import subprocess
import traceback


ROOT=Path(__file__).resolve().parents[4]
WORK=ROOT/'temp/sn100_host/qc_history_integration_20261026'
OUT=ROOT/'04_references/sn100_host/qc_history_integration_20261026/post_platform'
ATTEMPT=WORK/'archive_attempts/post_platform_archive_02'


def need(ok,why):
    if not ok:raise RuntimeError(why)


def sha(value):return hashlib.sha256(value).hexdigest()


def raw(path):
    need(path.is_file() and not path.is_symlink() and path.resolve().is_relative_to(ROOT),'Not ordinary workspace text')
    value=path.read_bytes();need(b'\0' not in value,'NUL requires review');value.decode('utf-8',errors='strict');return value


def read(path):return json.loads(raw(path))


def save(path,value,exclusive=True):
    with path.open('xb' if exclusive else 'wb') as handle:handle.write((json.dumps(value,ensure_ascii=False,indent=2)+'\n').encode('utf-8'))


def main():
    need(not (OUT/'INCOMPLETE.json').exists(),'Incomplete archive')
    copied=read(OUT/'copied_inputs.json')
    for name,item in copied.items():
        need(len(raw(OUT/name))==item['bytes'] and sha(raw(OUT/name))==item['sha256'],'Archive copy changed')
        need(len(raw(ROOT/item['source']))==item['bytes'] and sha(raw(ROOT/item['source']))==item['sha256'],'Selected original changed')
    dest=OUT/'collection_receipts';dest.mkdir()
    stem=WORK/'integration_drivers/post_platform_archive_collection_02'
    receipt=read(Path(str(stem)+'.exit.json'));need(receipt['exit_code']==0 and receipt['timed_out'] is False,'Collector did not really exit0')
    sources=[Path(str(stem)+s) for s in ('.command.json','.process.json','.exit.json','.stdout.log','.stderr.log')]
    sources += [ATTEMPT/'result.json',Path(__file__).resolve()]
    extra={}
    for source in sources:
        value=raw(source);target=dest/source.name
        with target.open('xb') as handle:handle.write(value)
        need(raw(target)==value,'Collector receipt copy differs')
        extra[target.relative_to(OUT).as_posix()]={'source':source.relative_to(ROOT).as_posix(),'bytes':len(value),'sha256':sha(value)}
    argv=['git','check-ignore','-v','--',OUT.relative_to(ROOT).as_posix()+'/handoff.json']
    save(dest/'git_ignore.command.json',{'argv':argv,'cwd':str(ROOT)})
    process=subprocess.run(argv,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=15)
    save(dest/'git_ignore.exit.json',{'exit_code':process.returncode,'expected_exit_for_not_ignored':1})
    (dest/'git_ignore.stdout.log').write_bytes(process.stdout);(dest/'git_ignore.stderr.log').write_bytes(process.stderr)
    raw(dest/'git_ignore.stdout.log');raw(dest/'git_ignore.stderr.log')
    need(process.returncode==1 and not process.stdout and not process.stderr,'Archive ignored or git failure')
    save(OUT/'collection_receipt_inputs.json',extra)
    proof=read(OUT/'archive_verification.json');proof['actual_metadata_collector_exit']=receipt
    proof['checks']['actual_collector_exit_zero']=True;proof['checks']['git_not_ignored_actual_exit_one']=True
    save(OUT/'archive_verification.json',proof,False)
    handoff=read(OUT/'handoff.json');handoff['actual_metadata_collector_exit']=0
    handoff['collector_receipt']='collection_receipts/post_platform_archive_collection_02.exit.json'
    handoff['collector_first_attempt_failure']='collection_history/attempt_01; actual exit1 before formal copy, due to catalog-owner final proof adding checks/closure counts to raw exit receipt. Corrected only collector comparison; no business rerun.'
    handoff['git_ignore_actual_exit']=1;save(OUT/'handoff.json',handoff,False)
    paths=sorted(x for x in OUT.rglob('*') if x.is_file() and x!=OUT/'SHA256SUMS')
    hashes={p.relative_to(OUT).as_posix():{'bytes':len(raw(p)),'sha256':sha(raw(p))} for p in paths}
    (OUT/'SHA256SUMS').write_text(''.join(v['sha256']+'  '+name+'\n' for name,v in hashes.items()),encoding='utf-8',newline='\n')
    need(all(len(raw(OUT/name))==v['bytes'] and sha(raw(OUT/name))==v['sha256'] for name,v in hashes.items()),'Final readback differs')
    for name,item in copied.items():need(sha(raw(ROOT/item['source']))==item['sha256'],'Original changed at finalization')
    for name,item in extra.items():need(sha(raw(ROOT/item['source']))==item['sha256'],'Collector receipt input changed')
    result={'passed':True,'files':len(hashes)+1,'sha_entries':len(hashes),'copied_original_text_files':len(copied),
            'collector_actual_exit':receipt['exit_code'],'handoff_sha256':sha(raw(OUT/'handoff.json')),
            'sha256sums_sha256':sha(raw(OUT/'SHA256SUMS')),'archive_verification_sha256':sha(raw(OUT/'archive_verification.json')),
            'copied_inputs_sha256':sha(raw(OUT/'copied_inputs.json'))}
    save(ATTEMPT/'final_archive_readback.json',result);print(json.dumps(result,ensure_ascii=False))


if __name__=='__main__':
    try:main()
    except Exception as error:
        failure={'passed':False,'error':str(error),'traceback':traceback.format_exc(),'no_business_stage_rerun':True}
        if ATTEMPT.is_dir() and not (ATTEMPT/'finalization_failure.json').exists():save(ATTEMPT/'finalization_failure.json',failure)
        if OUT.is_dir() and not (OUT/'INCOMPLETE.json').exists():save(OUT/'INCOMPLETE.json',failure)
        raise
