"""将已冻结候选复制到本轮编译目录，核前像、后像和窄补丁重放。"""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[3]
work = Path(__file__).resolve().parent
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
kind = sys.argv[1]
if kind == 'ascii':
    source = work.parent / 'resume_ascii_input_20261105_01'
    patch = source / 'soft_line_break.patch'
    rows = json.loads((source / 'changed_files.json').read_text())
    files = [('06_programs/sn100_host/' + row['path'], source / 'tree' / row['path'], row['before_sha256'], row['after_sha256']) for row in rows]
    patch_prefix = '06_programs/sn100_host'
elif kind == 'hospital':
    source = work.parent / 'resume_hospital_draft_20261105_01'
    patch = source / 'hospital_remove_preserve_draft.patch'
    rows = json.loads((source / 'file_hashes.json').read_text())['files']
    files = [(row['file'], source / 'after' / row['file'], row['beforeSha256'], row['afterSha256']) for row in rows]
    patch_prefix = ''
elif kind == 'barcode':
    source = work.parent / 'resume_barcode_draft_20261105_01'
    patch = source / 'packaging_01/barcode_delete_draft.patch'
    old_rows = json.loads((source / 'source_before.json').read_text())
    new_rows = json.loads((source / 'source_after.json').read_text())
    assert old_rows.keys() == new_rows.keys() and len(old_rows) == 1
    files = [('06_programs/sn100_host/' + rel, source / 'candidate/06_programs/sn100_host' / rel, old_rows[rel], new_rows[rel]) for rel in old_rows]
    patch_prefix = ''
else:
    raise ValueError(kind)
stage = work / 'compile_inputs' / kind
stage.mkdir(parents=True, exist_ok=False)
replay = work / 'patch_replay' / kind
replay.mkdir(parents=True, exist_ok=False)
before = json.loads((work / 'formal_before.json').read_text())
for rel, candidate, old, new in files:
    assert sha(root / rel) == old == before[rel.removeprefix('06_programs/sn100_host/')]
    assert sha(candidate) == new
    for folder, original in ((stage, candidate), (replay, root / rel)):
        target = folder / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(original, target)
        assert sha(original) == sha(target)
directory = (replay.relative_to(root) / patch_prefix).as_posix()
for check in (True, False):
    args = ['git', '-c', 'core.autocrlf=false', 'apply', *(['--check'] if check else []), '--directory=' + directory, str(patch)]
    result = subprocess.run(args, cwd=root, capture_output=True)
    label = 'check' if check else 'apply'
    (work / f'{kind}_patch_{label}.stdout').write_bytes(result.stdout)
    (work / f'{kind}_patch_{label}.stderr').write_bytes(result.stderr)
    assert result.returncode == 0, result.stderr.decode(errors='replace')
replay_checks = []
for rel, candidate, old, new in files:
    replay_checks.append({'file': rel, 'actual': sha(replay / rel), 'expected': new})
    assert sha(replay / rel) == new
report = {'kind': kind, 'patch_sha256': sha(patch), 'source_files': [rel for rel, *_ in files],
          'after_sha256': {rel: new for rel, _, _, new in files}, 'check_exit': 0, 'apply_exit': 0,
          'replay_byte_checks': replay_checks, 'formal_modified': False}
(work / f'frozen_{kind}.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report))
