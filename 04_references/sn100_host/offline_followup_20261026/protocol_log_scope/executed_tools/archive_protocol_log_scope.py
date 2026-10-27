"""Publish only completed narrow protocol evidence, preserving ordinary bytes."""
from pathlib import Path
import hashlib
import json
import shutil

if not __debug__:
    raise RuntimeError('Verification requires assertions enabled')
work = Path(__file__).resolve().parent
root = work.parents[2]
source = work / 'protocol_log_scope'
program = root / '06_programs/sn100_host'
dest = root / '04_references/sn100_host/offline_followup_20261026/protocol_log_scope'
read = lambda p: json.loads(p.read_text(encoding='utf-8-sig'))
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
n, w, a = (read(source / path / 'verification.json') for path in ('', 'windows', 'arm64'))
assert all(v['passed'] and v['source_unchanged'] for v in (n, w, a))
assert n['source_inputs'] == w['source_inputs'] == a['source_inputs']
assert n['old_scope_checker_actual_exit'] == 1 and n['old_actual_target_exit'] == 0
assert n['new_checker_actual_exit'] == w['qmake_exit'] == w['make_exit'] == 0
assert [row['actual_target_exit'] for row in n['cases']] == [0, 0, 0, 3, 3]
assert len(a['fresh_objects']) == 8 and all(code == 0 for code in a['all_child_exit_codes'])
assert all(sha(program / name) == digest for name, digest in n['source_inputs'].items())
assert sha(source / 'native_build/sn100_protocol_soak') == n['new_binary_sha256']
assert sha(source / 'arm64/sn100_protocol_soak') == a['binary_sha256']
win_binary = Path(w['binary']['path'].replace('C:/', '/mnt/c/'))
assert sha(win_binary) == w['binary']['sha256']
dest.mkdir(parents=True, exist_ok=False)
manifest = {}
def copy(path, relative):
    data = path.read_bytes()
    assert path.is_file() and not path.is_symlink() and len(data) < 16 * 1024 * 1024
    assert b'\x00' not in data and not data.startswith((b'SQLite format 3', b'MZ', b'\x7fELF'))
    target = dest / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    with target.open('xb') as stream:
        stream.write(data)
    assert sha(path) == sha(target)
    manifest[str(relative)] = {'source': str(path), 'sha256': sha(path), 'bytes': len(data)}
allowed = {'.json', '.jsonl', '.log', '.txt'}
for directory in (source, source / 'windows', source / 'arm64'):
    for path in sorted(directory.iterdir()):
        if path.is_file() and path.suffix in allowed:
            copy(path, path.relative_to(source))
for name in ('old_scope', 'new_cli'):
    for path in sorted((source / name).rglob('*')):
        if path.is_file() and not path.is_symlink() and path.suffix in allowed:
            copy(path, path.relative_to(source))
for name in n['source_inputs']:
    copy(program / name, Path('source') / name)
for name in ('run_protocol_log_scope.py', 'build_protocol_windows.ps1', 'build_protocol_arm64.py', 'archive_protocol_log_scope.py'):
    copy(work / name, Path('executed_tools') / name)
copy(root / 'temp/sn100_host/qc_local_20261025/integration_drivers/windows/file_helpers.ps1', Path('executed_tools/windows_file_helpers.ps1'))
for name in ('native_build/Makefile', 'windows/Makefile'):
    copy(source / name, Path(name + '.txt'))
info = {'schema': 'sn100-protocol-log-scope-followup-v1', 'status': 'observation_gap_fixed_and_narrow_checks_passed',
    'source_inputs': n['source_inputs'], 'changed_official_files': ['protocol/soak_main.cpp', 'tests/protocol_soak_timing_test.py'],
    'preimages': '07_backups/sn100_host/offline_followup_20261026/protocol_log_scope_preimage/manifest.json',
    'native_checker_actual_exit': 0, 'native_target_exits': [0, 0, 0, 3, 3],
    'old_target_actual_exit': 0, 'new_scope_checker_on_old_binary_exit': 1,
    'meaning_of_old_contrast': 'Old functional run passed; the new complete-scope validator rejects only absent markers',
    'builds': {'native': n['new_binary_sha256'], 'windows32': w['binary']['sha256'], 'arm64': a['binary_sha256']},
    'windows_or_arm_product_executed': False, 'new_full_app_release_or_gui_claimed': False,
    'old8h_direct_cause_confirmed': False, 'old8h_defect_fixed_claimed': False,
    'observed_gap': 'Previous log.write span omitted size/rotation and stdout; new optional complete scopes pair before synchronous failure saving',
    'contracts_unchanged': ['120ms timeout', 'one retry', '8192 bounded ring', 'default timing off', 'failure remains failure', 'queued/write is not peer ACK'],
    'limits': ['No inference of the original19min10s cause from an aggregated heartbeat maximum',
        '25min pass does not close8h or exclude a rare stall; its overwritten ring cannot reconstruct old1150s events',
        'More events shorten retained time at a fixed8192 capacity; do not compare old/new retained duration as equal',
        'No new CPU/RSS/input-response budget or long-stability/real instrument qualification'],
    'runtime_databases_ini_credentials_objects_binaries_copied': False, 'copied_files': manifest}
(dest / 'handoff.json').write_text(json.dumps(info, ensure_ascii=False, indent=2) + '\n')
lines = [sha(p) + '  ' + p.relative_to(dest).as_posix() for p in sorted(dest.rglob('*')) if p.is_file()]
(dest / 'SHA256SUMS').write_text('\n'.join(lines) + '\n')
for row in lines:
    digest, name = row.split('  ', 1)
    assert sha(dest / name) == digest
print(json.dumps({'handoff': str(dest / 'handoff.json'), 'verified_files': len(lines), 'old8h_cause_confirmed': False}, ensure_ascii=False))
