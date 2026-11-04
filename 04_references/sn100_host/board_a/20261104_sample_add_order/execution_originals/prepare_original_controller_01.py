"""只打包用户原配套材料；不使用板B运行数据，不运行程序。"""
from pathlib import Path,PurePosixPath
import hashlib,io,json,tarfile
ROOT=Path(__file__).resolve().parents[4]
HERE=Path(__file__).resolve().parent
source=ROOT/'05_tools/tests/sn100_host/SN500'
current=ROOT/'05_tools/tests/sn100_host/current'
archive=ROOT/'07_backups/sn100_host/board_a/sample_order_20261104_01/controller_inputs'
archive.mkdir(parents=True)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
binary=current/'ACA_V1.0OffLineD-500'
assert sha(binary)=='ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff'
mapping={}
for path in source.rglob('*'):
    if not path.is_file():continue
    relative=path.relative_to(source).as_posix()
    if relative.startswith(('core/','GL4A_LOG/')):continue
    if path.suffix not in ('.db','.ini','.cfg','.so','.csv') and not path.name.endswith(('.db-wal','.db-shm')):continue
    assert not path.is_symlink()
    mapping[relative]=path
mapping['ACA_V1.0OffLineD-500']=binary
mapping['theme_config.ini']=current/'theme_config.ini'
records=[{'member':name,'source':path.relative_to(ROOT).as_posix(),'sha256':sha(path),'bytes':path.stat().st_size} for name,path in sorted(mapping.items())]
checksums=''.join(row['sha256']+'  '+row['member']+'\n' for row in records).encode()
bundle=archive/'sn500_user_original_inputs.tar.gz'
with bundle.open('xb') as output:
    with tarfile.open(fileobj=output,mode='w:gz') as tar:
        for name,path in sorted(mapping.items()):
            raw=path.read_bytes();info=tarfile.TarInfo(name);info.size=len(raw);info.mode=0o755 if name=='ACA_V1.0OffLineD-500' else 0o644
            tar.addfile(info,io.BytesIO(raw))
        info=tarfile.TarInfo('INPUT_SHA256SUMS');info.size=len(checksums);info.mode=0o644;tar.addfile(info,io.BytesIO(checksums))
with tarfile.open(bundle,'r:gz') as tar:
    assert {m.name for m in tar}==set(mapping)|{'INPUT_SHA256SUMS'}
    for row in records:assert hashlib.sha256(tar.extractfile(row['member']).read()).hexdigest()==row['sha256']
manifest={'board':'board_a','scope':'new_original_controller_installation_from_user_inputs_not_board_b_runtime',
    'package':{'path':bundle.relative_to(ROOT).as_posix(),'sha256':sha(bundle),'bytes':bundle.stat().st_size},'files':records,
    'omitted':'old logs, core dumps, old executable and startup/build/stop scripts; no old kill/chmod/cleanup script is executed',
    'restore':'Only new /userdata/shared contents from these user inputs; does not restore SN100 accounts or patient data.'}
(archive/'provenance.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
(archive/'SHA256SUMS').write_text(''.join(sha(p)+'  '+p.name+'\n' for p in sorted(archive.iterdir()) if p.is_file()),encoding='ascii')
print(json.dumps({'members':len(records),'package':manifest['package']},ensure_ascii=False))
