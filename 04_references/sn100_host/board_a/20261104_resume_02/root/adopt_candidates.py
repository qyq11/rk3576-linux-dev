"""逐片核前后字节后采用三份独立候选，不接纳旧整树。"""
from pathlib import Path
import hashlib
import importlib.util
import json
import subprocess

root = Path(__file__).resolve().parents[3]
work = Path(__file__).resolve().parent
temp = work.parent
program = root / '06_programs/sn100_host'
spec = importlib.util.spec_from_file_location('package_arm64', program / 'shell/package_arm64.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
baseline = json.loads((root / '07_backups/sn100_host/board_a/formal_merge_20261104_01/application/source_snapshot.json').read_text())
before = {key.replace('\\', '/'): value for key, value in module.snapshot(program).items()}
assert before == baseline and len(before) == 697
(work / 'adoption_before.json').write_text(json.dumps(before, indent=2) + '\n')
ascii_dir = temp / 'resume_ascii_20261104_02'
sample_dir = temp / 'resume_sample_20261104_02'
lis_dir = temp / 'resume_lis_B1_20261104_02'
ascii_changes = json.loads((ascii_dir / 'changed_files.json').read_text())
sample_changes = json.loads((sample_dir / 'file_hashes.json').read_text())['files']
lis_changes = json.loads((lis_dir / 'target_changes.json').read_text())
jobs = [
    ('ascii_timestamp', ascii_dir / 'timestamp_colour.patch', ['--directory=06_programs/sn100_host'],
     [(f'06_programs/sn100_host/{x["path"]}', x['before_sha256'], x['after_sha256']) for x in ascii_changes]),
    ('sample_focus', sample_dir / 'sample_container_focus.patch', [],
     [(x['file'], x['beforeSha256'], x['afterSha256']) for x in sample_changes]),
    ('lis_snapshot', lis_dir / 'packaging_01/lis_B1_snapshot_auto_id.patch', [],
     [(f'06_programs/sn100_host/{x["path"]}', x['before'], x['after']) for x in lis_changes]),
]
expected = {}
for name, patch, flags, files in jobs:
    for path, old, new in files:
        assert path not in expected and sha(root / path) == old, path
        expected[path] = new
    subprocess.run(['git', 'apply', '--check', *flags, str(patch)], cwd=root, check=True)
results = []
for name, patch, flags, files in jobs:
    result = subprocess.run(['git', 'apply', *flags, str(patch)], cwd=root, capture_output=True)
    (work / f'adopt_{name}.stdout').write_bytes(result.stdout)
    (work / f'adopt_{name}.stderr').write_bytes(result.stderr)
    assert result.returncode == 0, name
    for path, old, new in files:
        assert sha(root / path) == new, path
    results.append({'slice': name, 'patch_sha256': sha(patch), 'check_exit': 0, 'apply_exit': result.returncode, 'files': len(files)})
after = {key.replace('\\', '/'): value for key, value in module.snapshot(program).items()}
changed = {key for key in after if after[key] != before[key]}
assert changed == {str(Path(path).relative_to('06_programs/sn100_host')).replace('\\', '/') for path in expected}
assert len(after) == 697 and len(changed) == 6
(work / 'adoption_after.json').write_text(json.dumps(after, indent=2) + '\n')
(work / 'adoption_result.json').write_text(json.dumps({'slices': results, 'source_count': len(after), 'changed_files': sorted(changed), 'other_sources_unchanged': 691}, indent=2) + '\n')
print(json.dumps(results, indent=2))
