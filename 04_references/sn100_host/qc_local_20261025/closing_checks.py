"""Check final source/documents/restore references without rerunning workloads."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tomllib

if not __debug__:
    raise RuntimeError('Closing checks require assertions enabled')
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root-go', action='store_true', required=True)
args = parser.parse_args()
assert args.root_go
formal = Path(__file__).resolve().parent
root = formal.parents[2]
work = root / 'temp/sn100_host/qc_local_20261025'
read = lambda p: json.loads(p.read_text(encoding='utf-8-sig'))
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
freeze = work / 'native/final_before.json'
after = work / 'source_after_documents.json'
assert sha(freeze) == sha(after) == '5123bf1442472450bbde23a59fc47287dc85ead34e006c523a96a016ac76cf29'
assert read(freeze) == read(after) and len(read(after)) == 426
v = read(formal / 'final/verification.json')
assert v['passed'] and v['source_inputs'] == 426 and v['suite_invocations'] == 35
assert sha(formal / 'final/verification.json') == '57f2a3d2b11599e5e3154bac8289c9cfaa64de1bfe1fdf74571abc355383a51c'
assert sha(formal / 'final/SHA256SUMS') == '71bd4426ba28a53dcf208835f8554b11d600085c733298fb64be907970d8f0f3'
manifest = (formal / 'final/SHA256SUMS').read_text().splitlines()
assert len(manifest) == len({line.split('  ', 1)[1] for line in manifest}) == 3290
assert all(t[1:] == [0, 0] for t in v['qt_totals_by_invocation'].values())
handoff = read(formal / 'handoff.json')
assert handoff['status'] == 'passed_local_slice_only'
assert len(handoff['document_preimages']['files']) == 7
for name, digest in handoff['document_preimages']['files'].items():
    assert sha(root / handoff['document_preimages']['directory'] / name) == digest
for ref in handoff['independently_preserved_handoffs']:
    assert sha(root / ref['path']) == ref['sha256']
for name in handoff['original_basis']:
    assert (root / name).is_file()
documents = ['README.md', '06_programs/sn100_host/README.md',
    '01_docs/knowledge/sn100_host_project_guide.md', '01_docs/plans/next_steps.md',
    '01_docs/plans/development_plan.md', '01_docs/plans/sn100_host_migration_plan.md',
    '02_records/20261022_sn100_host_migration.md']
plans = documents[3:6]
for name in plans:
    body = (root / name).read_text()
    for heading in ('## 一、已完成', '## 二、当前需要做', '## 三、未完成即将执行', '## 四、将来需要做'):
        assert heading in body, (name, heading)
assert '- [ ] **语言设置。**' in (root / plans[2]).read_text()
assert '- [ ] **有RK后：**' in (root / plans[0]).read_text()
checked_links = []
for name in documents[:6] + ['07_backups/sn100_host/offline_four_windows_20261025/README.md']:
    path = root / name
    body = path.read_text()
    # All recovery links, plus the current top-of-document links. Existing
    # historical code examples elsewhere are outside this narrow final check.
    selected = body if name.startswith('07_backups/') else '\n'.join(body.splitlines()[:17])
    for link in re.findall(r'\]\(([^)]+)\)', selected):
        if '://' in link or link.startswith('#'):
            continue
        target = link.split('#', 1)[0]
        assert (path.parent / target).resolve().is_file(), (name, link)
        checked_links.append({'document': name, 'target': link})
encoding = read(formal / 'collector_capture_encoding_handoff.json')
assert encoding['passed'] and encoding['original_collector_exit_code'] == 1 and encoding['fixed_collector_exit_code'] == 0
assert encoding['first_partial_stage_preserved'] and len(encoding['captured_bytes']) == 3
git_paths = ['04_references/sn100_host/qc_local_20261025/final/verification.json',
    '04_references/sn100_host/qc_local_20261025/handoff.json',
    '04_references/sn100_host/qc_local_20261025/collector_capture_encoding_handoff.json',
    '07_backups/sn100_host/offline_four_windows_20261025/README.md']
ignored = subprocess.run(['git', 'check-ignore', '-v', '--', *git_paths], cwd=root, text=True, capture_output=True)
assert ignored.returncode == 1 and not ignored.stdout and not ignored.stderr
attributes = read(formal / 'recovery_git_attributes.json')
assert attributes['all_archive_attributes_lfs'] and attributes['none_ignored']
automation = tomllib.loads(Path('/mnt/c/Users/52400/.codex/automations/sn100-10/automation.toml').read_text())
result = {'schema': 'sn100-four-windows-closing-checks-v1', 'passed': True,
    'scope': 'Final document/source/restore-reference checks; complete workload/3291-file full rereads were performed by successful collector session87729',
    'source_inputs': 426, 'post_document_snapshot_sha256': sha(after), 'source_unchanged_after_document_updates': True,
    'final_verification_sha256': sha(formal / 'final/verification.json'), 'final_manifest_sha256': sha(formal / 'final/SHA256SUMS'),
    'collector_reported_verified_evidence_files': 3291, 'document_preimages_verified': 7,
    'documents_sha256': {name: sha(root / name) for name in documents}, 'critical_links_checked': checked_links,
    'root_handoff_sha256': sha(formal / 'handoff.json'), 'encoding_handoff_sha256': sha(formal / 'collector_capture_encoding_handoff.json'),
    'actual_final_paths_not_ignored': git_paths, 'git_check_ignore_exit': ignored.returncode,
    'concrete_archive_lfs_receipt': 'recovery_git_attributes.json',
    'automation': {'id': automation['id'], 'status': automation['status'], 'pause_verified': False},
    'round_offline_gates_complete': False, 'board_or_instrument_used': False, 'git_committed_or_pushed': False}
with (formal / 'closing_verification.json').open('x', encoding='utf-8') as stream:
    stream.write(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
print(json.dumps({'passed_document_source_checks': True, 'source_inputs': 426, 'links': len(checked_links),
    'automation_status_before_final_pause': automation['status'], 'output': str(formal / 'closing_verification.json')}, ensure_ascii=False))
