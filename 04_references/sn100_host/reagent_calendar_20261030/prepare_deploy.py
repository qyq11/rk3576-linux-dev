"""RK针对验证通过后，仅准备受影响应用的恢复输入。"""
from pathlib import Path
import hashlib,json,shutil,subprocess,tarfile
w=Path(__file__).resolve().parent;r=w.parents[2]
for name in ('calendar_all_02','calendar_eglfs_01'):
    assert (w/name/'exit_code').read_text().strip()=='0',name
    assert '0 failed' in (w/name/'app.log').read_text(),name
assert (w/'build_exit_code').read_text().strip()=='0'
old=r/'07_backups/sn100_host/resume_20261030'
subprocess.run(['sha256sum','-c','SHA256SUMS'],cwd=old,check=True)
d=r/'07_backups/sn100_host/reagent_calendar_20261030';d.mkdir(parents=True,exist_ok=True)
before=subprocess.check_output(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=8','lckfb@192.168.100.123',
 'base=/home/rkshare/programs/sn100_shell; readlink "$base/current"; cat "$base/previous_release"; sha256sum "$base/current/SHA256SUMS" "$base/sn100.sh"; pgrep -x sn100_shell || true; flock -n "$base/lifecycle.lock" -c true'])
assert before.decode().splitlines()[0]=='releases/a247d2f89c4914655730f61217eb98bbbcfedeb983a14e4c86b0daaec8d90f7e'
(d/'application_before.txt').write_bytes(before)
package=Path((w/'packages/latest_package').read_text().strip());shutil.copy2(package,d/package.name)
cache=Path((w/'source_cache_path').read_text().strip());identity=json.loads((w/'all_source_identity.json').read_text());test=w/'test_inputs/original_main_task_chain_rk_test.cpp';identity['samples/tests/original_main_task_chain_rk_test.cpp']=hashlib.sha256(test.read_bytes()).hexdigest()
with tarfile.open(d/'sn100_calendar_sources.tar.gz','w:gz') as archive:
    for name,digest in sorted(identity.items()):
        source=test if name=='samples/tests/original_main_task_chain_rk_test.cpp' else cache/name;assert hashlib.sha256(source.read_bytes()).hexdigest()==digest,name
        archive.add(source,arcname='06_programs/sn100_host/'+name,recursive=False)
(d/'source_bundle_identity.json').write_text(json.dumps(identity,indent=2)+'\n')
shutil.copy2(w/'source_snapshot.json',d/'source_snapshot.json');shutil.copy2(w/'test_input_revision.json',d/'test_input_revision.json')
files=sorted(p for p in d.iterdir() if p.is_file() and p.name not in ('README.md','SHA256SUMS'))
(d/'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name+'\n' for p in files))
release_id=package.name.removeprefix('sn100_shell_').removesuffix('.tar.gz')
f=w/'final_board.sh';f.write_text(f.read_text().replace('a247d2f89c4914655730f61217eb98bbbcfedeb983a14e4c86b0daaec8d90f7e',release_id))
print('RECOVERY_INPUTS_READY='+str(d))
