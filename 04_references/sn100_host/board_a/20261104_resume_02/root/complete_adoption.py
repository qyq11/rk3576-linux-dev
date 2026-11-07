from pathlib import Path
import hashlib
import importlib.util
import json
import subprocess

root = Path(__file__).resolve().parents[3]
work = Path(__file__).resolve().parent
temp = work.parent
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
sample = temp / 'resume_sample_20261104_02'
normalizations = []
for row in json.loads((sample / 'file_hashes.json').read_text())['files']:
    path, candidate = root / row['file'], sample / 'after' / row['file']
    assert path.read_bytes().replace(b'\r\n', b'\n') == candidate.read_bytes()
    assert sha(candidate) == row['afterSha256']
    normalizations.append({'file': row['file'], 'git_apply_sha256': sha(path), 'expected_sha256': sha(candidate), 'only_crlf_to_lf': True})
    path.write_bytes(candidate.read_bytes())
lis = temp / 'resume_lis_B1_20261104_02'
patch = lis / 'packaging_01/lis_B1_snapshot_auto_id.patch'
rows = json.loads((lis / 'target_changes.json').read_text())
for row in rows:
    assert sha(root / '06_programs/sn100_host' / row['path']) == row['before']
for action in ('--check', None):
    result = subprocess.run(['git', 'apply', *([action] if action else []), str(patch)], cwd=root, capture_output=True)
    stage = 'check' if action else 'apply'
    (work / f'adopt_lis_snapshot_{stage}.stdout').write_bytes(result.stdout)
    (work / f'adopt_lis_snapshot_{stage}.stderr').write_bytes(result.stderr)
    assert result.returncode == 0
for row in rows:
    assert sha(root / '06_programs/sn100_host' / row['path']) == row['after']
ascii_dir = temp / 'resume_ascii_20261104_02'
for row in json.loads((ascii_dir / 'changed_files.json').read_text()):
    assert sha(root / '06_programs/sn100_host' / row['path']) == row['after_sha256']
spec = importlib.util.spec_from_file_location('packaging', root / '06_programs/sn100_host/shell/package_arm64.py')
module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
before = json.loads((work / 'adoption_before.json').read_text())
after = {k.replace('\\', '/'): v for k, v in module.snapshot(root / '06_programs/sn100_host').items()}
changed = sorted(k for k in after if after[k] != before[k])
assert len(after) == len(before) == 697 and len(changed) == 6
(work / 'adoption_after.json').write_text(json.dumps(after, indent=2) + '\n')
report = {'source_files': 697, 'changed': changed, 'unchanged': 691, 'three_patch_check_apply_exit': [0, 0, 0, 0, 0, 0], 'sample_line_ending_reconciliation': normalizations,
          'preliminary_baseline_build': '在合入前误启动，已仅中断本轮编译器，工具返回1、内部构建143，未部署；完整新编译另用arm64_build_02空目录。'}
(work / 'adoption_result.json').write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n')
print(json.dumps(report, ensure_ascii=False))
