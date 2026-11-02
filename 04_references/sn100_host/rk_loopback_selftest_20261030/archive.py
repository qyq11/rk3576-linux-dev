"""只归档通信、退出和资源证据，不收数据库或临时密码。"""
from pathlib import Path
import hashlib,json,shutil
w=Path(__file__).resolve().parent;r=w.parents[2];d=r/'04_references/sn100_host/rk_loopback_selftest_20261030';d.mkdir(parents=True,exist_ok=True)
for p in w.iterdir():
    if p.is_file() and p.suffix in ('.sh','.py','.json','.txt'):
        shutil.copy2(p,d/p.name)
for name in ['startup_failure_01','completed_01']:
    source=w/name;out=d/name;out.mkdir(exist_ok=True)
    for p in source.iterdir():
        if p.is_file() and p.name!='bootstrap_secret' and p.suffix not in ('.db','.sqlite'):
            shutil.copy2(p,out/p.name)
# 绑定本次使用的正式源文件，证明未为自测改业务代码。
program=r/'06_programs/sn100_host';paths=['shell/main.cpp','shell/device_status_shell.cpp','protocol/status_loopback.cpp','protocol/status_loopback.h','protocol/frame_codec.cpp']
identity={p:hashlib.sha256((program/p).read_bytes()).hexdigest() for p in paths}
(d/'source_identity.json').write_text(json.dumps(identity,indent=2)+'\n')
files=sorted(p for p in d.rglob('*') if p.is_file() and p.name not in ('README.md','SHA256SUMS'))
(d/'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+str(p.relative_to(d))+'\n' for p in files))
print('ARCHIVED_FILES='+str(len(files)))
