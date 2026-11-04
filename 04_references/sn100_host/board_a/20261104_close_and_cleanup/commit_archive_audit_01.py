"""提交前只读核归档、敏感文件和源码；不执行产品，不连接板卡。"""
from pathlib import Path
import hashlib,json,re,subprocess,sys
ROOT=Path(__file__).resolve().parents[4]
HERE=Path(__file__).resolve().parent
def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()
untracked=subprocess.run(['git','ls-files','--others','--exclude-standard','-z'],cwd=ROOT,capture_output=True,check=True).stdout.decode().split('\0')
paths=[ROOT/name for name in untracked if name]
roots=[ROOT/'04_references/sn100_host',ROOT/'07_backups/sn100_host']
manifests=sorted({p for base in roots for p in base.rglob('SHA256SUMS') if any(p.is_relative_to(root) for root in [ROOT/'04_references/sn100_host/board_a',ROOT/'04_references/sn100_host/board_b/20261103_integration_10_keep_running',ROOT/'04_references/sn100_host/board_b/20261104_camera_stopped_baseline',ROOT/'04_references/sn100_host/board_b/20261104_integration_11_file_phase_root',ROOT/'04_references/sn100_host/board_b/20261104_integration_12_transition_pending',ROOT/'04_references/sn100_host/operation_flow_audit_20261104',ROOT/'04_references/sn100_host/workflow_next_20261103',ROOT/'07_backups/sn100_host/board_a',ROOT/'07_backups/sn100_host/board_b/workflow_integration_20261103'])})
checked=0;failures=[]
for sums in manifests:
    for row in sums.read_text(encoding='utf-8-sig').splitlines():
        m=re.fullmatch('([a-fA-F0-9]{64})  (.+)',row)
        if not m:failures.append({'manifest':str(sums.relative_to(ROOT)),'reason':'unsupported_checksum_line'});continue
        name=m[2]
        if name.startswith(('/', 'C:', 'D:')):continue
        target=sums.parent/name
        if not target.is_file() or sha(target)!=m[1].lower():failures.append({'manifest':str(sums.relative_to(ROOT)),'member':name,'reason':'missing_or_hash_diff'})
        checked+=1
suspects=[]
for p in paths:
    if not p.is_file():continue
    if p.name in ('id_ed25519','id_rsa','authorized_keys') or p.suffix in ('.pem','.key') or p.name.startswith('.env'):
        suspects.append({'path':p.relative_to(ROOT).as_posix(),'reason':'credential_filename'})
    if p.stat().st_size>8*1024*1024:continue
    raw=p.read_bytes()
    if any(token in raw for token in (b'-----BEGIN OPENSSH PRIVATE KEY-----',b'-----BEGIN RSA PRIVATE KEY-----',b'-----BEGIN PRIVATE KEY-----')):
        suspects.append({'path':p.relative_to(ROOT).as_posix(),'reason':'private_key_marker'})
    if re.search(rb'\b(?:ghp_[A-Za-z0-9]{36}|github_pat_[A-Za-z0-9_]{60,})\b',raw):
        suspects.append({'path':p.relative_to(ROOT).as_posix(),'reason':'access_token_pattern'})
sources=json.loads((ROOT/'temp/sn100_host/workflow_next_20261103/integration_15_source_acceptance_01/sample_empty_backend/source_after.json').read_bytes())
source_drift=[name for name,digest in sources.items() if sha(ROOT/'06_programs/sn100_host'/name)!=digest]
report={'untracked_files':len(paths),'checksum_manifests':len(manifests),'checksum_entries_checked':checked,'checksum_failures':failures,'credential_suspects':suspects,'formal_sources':len(sources),'source_drift':source_drift,'product_executed':False}
with (HERE/'commit_archive_audit_01.json').open('x',encoding='utf-8') as f:json.dump(report,f,ensure_ascii=False,indent=2);f.write('\n')
print(json.dumps(report,ensure_ascii=False))
raise SystemExit(1 if failures or suspects or source_drift else 0)
