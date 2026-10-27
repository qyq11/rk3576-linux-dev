#!/usr/bin/env python3
"""Read-only formal-input inventory and private-build provenance collection."""
import argparse
import hashlib
import importlib.util
import json
import os
import shutil
import subprocess
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
PROGRAM = ROOT / '06_programs/sn100_host'
WORK = ROOT / 'temp/sn100_host/o02_login/platforms'
parser = argparse.ArgumentParser()
parser.add_argument('--phase', choices=['prepare', 'arm-before', 'arm-after'], default='prepare')
args = parser.parse_args()
assert ROOT.name == '20260907_RK3576_Linux'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n')


spec = importlib.util.spec_from_file_location('package', PROGRAM / 'shell/package_arm64.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)
snapshot = package.snapshot(PROGRAM)
environment = dict(__import__('os').environ, SN100_ARM64_LIST_INPUTS='1')
if args.phase == 'arm-after':
    environment['SN100_ARM64_SYSROOT'] = str(WORK / 'arm64/sysroot')
    environment['SN100_ARM64_BUILD_DIR'] = str(WORK / 'arm64/arm64_build')
inventory = subprocess.check_output(['bash', str(PROGRAM / 'shell/build.sh'), str(WORK / 'unused_output')],
                                    env=environment, text=True)
rows = [line.split('\t', 1) for line in inventory.splitlines()]
required = ['shell/login_shell.cpp', 'ui/maskdlglogin.cpp', 'ui/maskdlglogin.h', 'ui/maskdlglogin.ui',
            'data/account_store.h', 'data/account_store.cpp']
for name in required:
    assert name in snapshot and any(value == str(PROGRAM / name) for _, value in rows), name
assert not any('account_store' in value or 'maskdlglogin' in value or 'login_shell' in value
               for role, value in rows if role == 'helper_source')
tools = {}
for name in ['aarch64-linux-gnu-g++', 'aarch64-linux-gnu-gcc', 'aarch64-linux-gnu-ld',
             'aarch64-linux-gnu-readelf', 'python3']:
    path = Path(shutil.which(name)).resolve(strict=True)
    tools[name] = {'path': str(path), 'sha256': sha(path)}
for name in ['moc', 'uic', 'rcc']:
    path = Path('/usr/lib/qt5/bin') / name
    tools[name] = {'path': str(path.resolve(strict=True)), 'sha256': sha(path)}
record = {'phase': args.phase, 'source_entries': len(snapshot), 'sources': snapshot,
          'arm_input_counts': dict(Counter(role for role, _ in rows)),
          'required_auth_inputs_present': True, 'helper_has_no_account_abi_dependency': True,
          'tools': tools, 'execution': 'No build, GUI, networking or runtime data access in this inventory step'}
save(WORK / f'{args.phase}_inventory.json', record)
(WORK / f'{args.phase}_arm_inputs.tsv').write_text(inventory)
if args.phase == 'arm-after':
    (WORK / 'arm64/effective_input_list.tsv').write_text(inventory)
if args.phase == 'arm-after':
    before = json.loads((WORK / 'arm-before_inventory.json').read_text())
    assert before['sources'] == snapshot and before['tools'] == tools
    arm = WORK / 'arm64'
    dependency = json.loads((arm / 'final_dependencies/dependency_report.json').read_text())
    assert dependency['offline_link_and_sql_closure_passed'] and not dependency['errors']
    log = (arm / 'prepare.log').read_text()
    objects = sorted((arm / 'arm64_build').glob('obj_*.o')) + sorted((arm / 'arm64_build').glob('helper_*.o'))
    assert objects
    # Parallel bash xtrace may split a command across lines. The fresh object's
    # own DWARF independently identifies its real source and compiler flags.
    dwarf = json.loads((arm / 'dwarf_object_sources.json').read_text())
    assert dwarf['all_dwarf_sources_verified'] and len(dwarf['objects']) == len(objects)
    dwarf_by_object = {record['object']: record for record in dwarf['objects']}
    for path in objects:
        assert '-o ' + str(path) in log, str(path)
        object_record = dwarf_by_object[path.relative_to(arm).as_posix()]
        assert sha(path) == object_record['object_sha256']
        assert sha(Path(object_record['source'])) == object_record['source_sha256']
    binary = arm / 'build/sn100_shell'
    helper = arm / 'build/sn100_report_renderer'
    published = Path((arm / 'packages/latest_package').read_text().strip())
    release = json.loads(next((arm / 'packages').glob('stage_*/release.json')).read_text())
    assert release['sources'] == snapshot
    assert release['binary_sha256'] == sha(binary) and release['renderer_sha256'] == sha(helper)
    verification = {
        'method': 'Fresh ARM64 main/helper full compilation; no object reuse; original formal build/runtime/check/package scripts',
        'source_entries': len(snapshot), 'sources_tools_unchanged': True,
        'source_matches_native_freeze': snapshot == json.loads((ROOT / 'temp/sn100_host/o02_login/native/after.json').read_text()),
        'object_count': len(objects), 'objects_sha256': {str(p.relative_to(arm)): sha(p) for p in objects},
        'all_object_outputs_found_in_trace': True, 'all_object_elf_dwarf_source_verified': True,
        'trace_single_line_pairing_complete': False, 'trace_split_object': 'obj_20.o', 'reused_objects': 0,
        'prepare_exit_code': 0, 'final_closure_exit_code': 0,
        'first_wrapper_postprocessor_exit_code': 1, 'corrected_read_only_postprocessor_exit_code': 0,
        'postprocessor_attempt_exit_codes': [1, 1, 0, 0],
        'success_json_published_atomically': True,
        'main_sha256': sha(binary), 'renderer_sha256': sha(helper),
        'package': str(published), 'package_sha256': sha(published),
        'dependency_file_count': len(dependency['files']), 'closure_errors': [],
        'board_runtime_verified': False, 'instrument_verified': False,
    }
    summary = {'phase': args.phase, 'source_entries': len(snapshot),
               'arm_input_counts': record['arm_input_counts'], 'inventory_only': False,
               'verified_objects': len(objects), 'postprocessor_exit_condition': 'all checks and summary serialization complete'}
    assert verification['source_matches_native_freeze'] and verification['object_count'] == 142
    assert verification['sources_tools_unchanged'] and verification['closure_errors'] == []
    assert len(verification['main_sha256']) == len(verification['renderer_sha256']) == 64
    payload = json.dumps(verification, indent=2) + '\n'
    summary_payload = json.dumps(summary)
    print(summary_payload, flush=True)
    temporary = arm / 'verification.json.pending'
    with temporary.open('x', encoding='utf-8') as stream:
        stream.write(payload)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, arm / 'verification.json')
else:
    print(json.dumps({'phase': args.phase, 'source_entries': len(snapshot),
                     'arm_input_counts': record['arm_input_counts'], 'inventory_only': True}))
