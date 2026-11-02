"""仅在两项RK通过后准备原位置发布脚本与恢复输入。"""
import hashlib
import json
import pathlib
import shutil
import subprocess
import tarfile

work = pathlib.Path(__file__).resolve().parent
root = work.parents[2]
old = root / 'temp/sn100_host/original_db_20261027/integration'
for name in ('quality_02', 'batch_01', 'task_all_01', 'quality_eglfs_01', 'batch_eglfs_01'):
    assert (work / name / 'exit_code').read_text().strip() == '0', name
    assert '0 failed' in (work / name / 'app.log').read_text(), name
script = (old / 'deploy_prepared_02.sh').read_text()
script = script.replace('root=$(cd -- "$here/../../../.." && pwd)', 'root=' + str(root))
script = script.replace('work=$here/release_work_02', 'work=$here')
script = script.replace('grep -qx \'READY_FOR_TRANSFER=1\' "$work/logs/"*_prepare_*.log', 'test "$(cat "$work/build_exit_code")" = 0')
(work / 'deploy.sh').write_text(script)
destination = root / '07_backups/sn100_host/resume_20261030'
destination.mkdir(exist_ok=True, parents=True)
subprocess.run(['sha256sum', '-c', 'SHA256SUMS'], cwd=root / '07_backups/sn100_host/original_db_20261027', check=True)
before = subprocess.check_output(['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=8',
    'lckfb@192.168.100.123', 'base=/home/rkshare/programs/sn100_shell; readlink "$base/current"; cat "$base/previous_release"; sha256sum "$base/current/SHA256SUMS" "$base/sn100.sh"; pgrep -x sn100_shell || true; flock -n "$base/lifecycle.lock" -c true'])
(destination / 'application_before.txt').write_bytes(before)
package = pathlib.Path((work / 'packages/latest_package').read_text().strip())
shutil.copy2(package, destination / package.name)
cache = pathlib.Path((work / 'source_cache_path').read_text().strip())
identity = json.loads((work / 'all_source_identity.json').read_text())
quality = work / 'test_inputs/original_quality_pages_rk_test.cpp'
identity['tests/original_quality_pages_rk_test.cpp'] = hashlib.sha256(quality.read_bytes()).hexdigest()
with tarfile.open(destination / 'sn100_resume_sources.tar.gz', 'w:gz') as archive:
    for name in sorted(identity):
        source = quality if name == 'tests/original_quality_pages_rk_test.cpp' else cache / name
        assert hashlib.sha256(source.read_bytes()).hexdigest() == identity[name], name
        archive.add(source, arcname='06_programs/sn100_host/' + name, recursive=False)
(destination / 'source_bundle_identity.json').write_text(json.dumps(identity, indent=2) + '\n')
for name in ('source_snapshot.json', 'test_input_revision.json'):
    shutil.copy2(work / name, destination / name)
files = [p for p in destination.iterdir() if p.is_file() and p.name not in ('SHA256SUMS', 'README.md')]
(destination / 'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + p.name + '\n' for p in sorted(files)))
print('RECOVERY_INPUTS_READY=' + str(destination))
