from pathlib import Path, PurePosixPath
import argparse
import hashlib
import json
import shutil
import tarfile
from common import verify_native

if not __debug__:
    raise RuntimeError('Archive verification must not run with Python optimization/assertions disabled')

parser=argparse.ArgumentParser(description='Verify and archive the actual completed ARM application/source pair; no build or deployment')
parser.add_argument('--root-go',action='store_true',required=True)
parser.add_argument('--destination',type=Path)
args=parser.parse_args()
assert args.root_go
work=Path(__file__).resolve().parent.parent
root=work.parents[2]
program=root/'06_programs/sn100_host'
arm=work/'platforms/arm64'
result=json.loads((arm/'verification.json').read_text())
frozen=json.loads((work/'native/final_before.json').read_text())
verify_native(frozen)
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
assert frozen and len(frozen)==result['source_entries']
assert all((program/name).is_file() and sha(program/name)==value for name,value in frozen.items())
assert all(name in frozen for name in ('quality_control/qc_store.cpp','quality_control/quality_control_page.cpp',
                                      'calibration/calibration_store.cpp','calibration/calibration_page.cpp'))
assert result['source_before_after_and_package_equal_native_freeze']
assert all(result[key]==0 for key in ('build_exit_code','closure_exit_code','package_exit_code'))
assert not result['closure_errors']
dest=args.destination or root/'07_backups/sn100_host/offline_four_windows_20261025'
assert dest.is_absolute() and dest.resolve().is_relative_to((root/'07_backups/sn100_host').resolve())
if (dest/'verification.json').exists():
    assert json.loads((dest/'verification.json').read_text())['release_id']==result['package']['release_id']
dest.mkdir(parents=True,exist_ok=True)
def copy(source,target):
    assert source.is_file() and not source.is_symlink()
    if target.exists():assert sha(source)==sha(target)
    else:shutil.copyfile(source,target)
    assert sha(source)==sha(target)
app=Path(result['package']['package'])
source=Path(result['source_package']['path'])
assert sha(app)==result['package']['package_sha256']
assert sha(source)==result['source_package']['sha256']
for archive in (app,source):copy(archive,dest/archive.name)
with tarfile.open(dest/app.name,'r:gz') as bundle:
    files={}
    directories=0
    for member in bundle.getmembers():
        path=PurePosixPath(member.name)
        assert not path.is_absolute() and '..' not in path.parts
        assert member.isfile() or member.isdir()
        if member.isdir():directories+=1;continue
        assert path.as_posix() not in files
        files[path.as_posix()]=bundle.extractfile(member).read()
    manifest=files['SHA256SUMS']
    release=hashlib.sha256(manifest).hexdigest()
    assert files['release_id'].decode().strip()==release==result['package']['release_id']
    listed={}
    for line in manifest.decode().splitlines():
        expected,name=line.split('  ',1)
        assert name not in listed
        listed[name]=expected
        assert hashlib.sha256(files[name]).hexdigest()==expected
    assert set(files)==set(listed)|{'SHA256SUMS','release_id'}
    metadata=json.loads(files['release.json'])
    assert metadata['sources']==frozen
    assert metadata['lis_settings_scope']=={'storage':'explicit_isolated_settings_directory','communication':False,'serial_device_open':False,'auto_flags':'intent_only'}
    assert '--lis-settings' in metadata['startup_feature_arguments'] and 'local_lis_settings' in metadata['capabilities']
    assert '--quality-control' in metadata['startup_feature_arguments'] and 'quality_control_local' in metadata['capabilities']
    assert '--calibration' in metadata['startup_feature_arguments'] and 'calibration_local' in metadata['capabilities']
    elf=0
    for name,data in files.items():
        if data.startswith(b'\x7fELF'):
            assert data[4:6]==b'\x02\x01' and int.from_bytes(data[18:20],'little')==183,name
            elf+=1
with tarfile.open(dest/source.name,'r:gz') as bundle:
    members=bundle.getmembers()
    assert len(members)==len(frozen) and {member.name for member in members}==set(frozen)
    for member in members:
        assert member.isfile() and hashlib.sha256(bundle.extractfile(member).read()).hexdigest()==frozen[member.name]
copy(work/'native/final_before.json',dest/'source_snapshot.json')
copy(arm/'verification.json',dest/'arm64_verification.json')
info={'schema':'sn100-four-windows-local-recovery-v1','release_id':release,'source_inputs':len(frozen),
      'application':{'path':app.name,'bytes':app.stat().st_size,'sha256':sha(app),'files':len(files),'directories':directories},
      'source':{'path':source.name,'bytes':source.stat().st_size,'sha256':sha(source),'members':len(frozen)},
      'internal_members_verified':len(manifest.decode().splitlines()),'aarch64_elf_members':elf,
      'runtime_data_or_authentication_archived':False,
      'limits':'Cross-build/offline closure and paired immutable source provenance; not RK execution, real LIS/printing/instrument, approved budget or long stability.'}
(dest/'verification.json').write_text(json.dumps(info,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
(dest/'SHA256SUMS').write_text(''.join(sha(path)+'  '+path.name+'\n' for path in sorted(dest.iterdir()) if path.is_file() and path.name not in ('SHA256SUMS','README.md')),encoding='utf-8')
print(json.dumps(info,ensure_ascii=False))
