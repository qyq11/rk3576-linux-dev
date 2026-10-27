"""Audit existing login test artifacts; never inspect or publish runtime databases."""
import hashlib
import importlib.util
import json
from pathlib import Path
import re

root = Path('/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux')
program = root / '06_programs/sn100_host'
work = root / 'temp/sn100_host/o02_login/shell_test'
driver = work / 'production_test_driver'
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
build = root / 'temp/sn100_host/o02_login/native/wsl_release'
production = json.loads((driver / 'verification.json').read_text())
assert sha(build / 'sn100_shell') == production['application_sha256']
assert all(sha(build / name) == checksum for name, checksum in production['production_sha256_by_object'].items())
assert all(sha(program / 'tests' / name) == checksum for name, checksum in production['test_source_sha256'].items())
spec = importlib.util.spec_from_file_location('shell_package', program / 'shell/package_arm64.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)
frozen = json.loads((driver / 'source_before.json').read_text())
assert frozen == json.loads((driver / 'source_after.json').read_text()) == package.snapshot(program)
assert not list((work / 'run_01').rglob('*.secret'))
files = [driver / name for name in ('verification.json', 'production_objects_before.json',
                                  'source_before.json', 'source_after.json', 'build.log', 'production_driver.pro')]
results = {}
uuid_pattern = re.compile(rb'(?i)\b[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\b')
for qpa in ('offscreen', 'xcb'):
    directory = work / 'run_01' / qpa
    assert (directory / 'exit_code.txt').read_text().strip() == '0'
    log = (directory / 'qt.log').read_text()
    match = re.search(r'Totals: (\d+) passed, (\d+) failed,.*?, (\d+)ms', log)
    assert match and match.group(1) == '5' and match.group(2) == '0'
    lifecycle = json.loads((directory / 'evidence/product_lifecycle.json').read_text())
    assert lifecycle['application_sha256'] == production['application_sha256']
    assert lifecycle['normal_exit'] == 0 and lifecycle['stores_closed'] == 7
    assert lifecycle['requires_accounts_exit'] == 2 and not lifecycle['rejected_directory_created']
    guards = json.loads((directory / 'evidence/session_guards.json').read_text())
    assert guards['action_ready_preserved'] is False
    results[qpa] = {'passed': 5, 'failed': 0, 'elapsed_ms': int(match.group(3)), 'exit_code': 0,
                    'log': str((directory / 'qt.log').relative_to(work)),
                    'product_lifecycle': lifecycle,
                    'action_ready_before': False, 'action_ready_after': False, 'action_ready_unchanged': True,
                    'raw_field_note': 'action_ready_preserved=false records the preserved value, not a failed assertion'}
    files += [directory / name for name in ('exit_code.txt', 'environment.txt', 'qt.log', 'console.log')]
    files += sorted(path for path in (directory / 'evidence').iterdir() if path.is_file())
for path in files:
    assert 'runtime' not in path.relative_to(work).parts
    if path.suffix in ('.json', '.log', '.txt'):
        assert not uuid_pattern.search(path.read_bytes()), 'Unexpected UUID-shaped private value in ' + str(path)
output = {
    'schema': 'sn100-password-login-shell-verification-v1', 'results': results,
    'application_sha256': production['application_sha256'],
    'driver_sha256': sha(driver / 'password_login_shell_test'),
    'test_source_sha256': production['test_source_sha256'],
    'exact_production_object_count': production['production_object_count'],
    'linked_production_object_count': production['linked_object_count'], 'only_excluded_object': 'main.o',
    'production_sources_and_objects_unchanged_after_tests': True,
    'production_source_snapshot_count': len(frozen),
    'private_runtime_databases_excluded': True, 'remaining_transient_secret_files': 0,
    'selected_text_evidence_uuid_secret_candidates': 0,
    'visual_review': {
        'source': 'Login QDialog::grab only; not an entire desktop/compositor screenshot',
        'viewports': ['1600x900', '1920x1080'], 'original_card_size': '450x571',
        'input_and_button_bounds_visible': True, 'screenshots_taken_with_empty_password': True,
        'original_blue_white_card_and_field_positions_retained': True,
        'unknown_company_header_left_empty': True,
        'black_or_transparent_mask_pixels_do_not_verify_parent_background_composition': True,
        'original_windows_active_theme_verified': False},
    'boundaries': {'instrument_connected': False, 'rk_connected': False, 'deployed': False,
                   'actual_hardware_action_ready_verified': False,
                   'password_scope': 'Isolated anonymous local account database only',
                   'resource_budget_or_long_stability_verified': False},
    'artifact_paths': [str(path.relative_to(work)) for path in files]
}
target = work / 'verification.json'
assert not target.exists()
target.write_text(json.dumps(output, indent=2, ensure_ascii=False) + '\n')
files.append(target)
(work / 'ARTIFACT_SHA256SUMS').write_text(''.join(f'{sha(path)}  {path.relative_to(work).as_posix()}\n'
                                                for path in sorted(set(files))))
print(json.dumps({'qpa_results': {key: value['passed'] for key, value in results.items()},
                  'production_objects_unchanged': len(production['production_sha256_by_object']),
                  'verification_sha256': sha(target), 'evidence_files': len(set(files))}, indent=2))
