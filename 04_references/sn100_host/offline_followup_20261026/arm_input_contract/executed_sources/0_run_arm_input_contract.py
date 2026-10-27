#!/usr/bin/env python3
"""Run the existing read-only build/input contract, with actual bounded receipts."""
import argparse
import hashlib
import importlib.util
import os
from pathlib import Path
import re
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
PROGRAM = ROOT / '06_programs/sn100_host'
parser = argparse.ArgumentParser()
parser.add_argument('--root-go', action='store_true', required=True)
parser.add_argument('attempt')
args = parser.parse_args()
if not args.root_go or os.geteuid() == 0 or not re.fullmatch(r'[A-Za-z0-9_-]+', args.attempt):
    parser.error('Explicit ordinary-user go and fresh simple attempt required')
helper = HERE / 'protocol_peer_gate/run_01/executed_sources/temp/sn100_host/offline_followup_20261026/protocol_peer_gate/run_peer_gate.py'
if hashlib.sha256(helper.read_bytes()).hexdigest() != '2d2b92f533706ba82fc5877ed76fd3b8115f2a72320e9e3c4dfc030d0656b264':
    raise RuntimeError('Frozen bounded execution helper changed')
spec = importlib.util.spec_from_file_location('frozen_receipts', helper)
receipts = importlib.util.module_from_spec(spec)
spec.loader.exec_module(receipts)
output = HERE / ('arm_input_contract_' + args.attempt)
output.mkdir()
inputs = [Path(__file__).resolve(), helper] + [PROGRAM / name for name in (
    'tests/test_shell_arm64_inputs.py', 'shell/build.sh', 'shell/package_arm64.py',
    'projects/projects_core.pri', 'reagents/reagents.pri', 'samples/samples_core.pri',
    'quality_control/qc_core.pri', 'quality_control/qc_widgets.pri', 'calibration/calibration.pri')]
before = {str(path): receipts.sha(path) for path in inputs}
receipts.save(output / 'inputs_before.json', before)
for index, path in enumerate(inputs):
    target = output / 'executed_sources' / (str(index) + '_' + path.name)
    target.parent.mkdir(exist_ok=True)
    target.write_bytes(path.read_bytes())
    if receipts.sha(target) != before[str(path)]:
        raise RuntimeError('Contract source snapshot changed')
passed = False
try:
    receipts.execute('contract', [sys.executable, '-B', str(PROGRAM / 'tests/test_shell_arm64_inputs.py'), '-v'],
                     output, ROOT, 60, {'SN100_ARM64_INPUT_TEST_DIR': str(output / 'anonymous_fixtures')})
    passed = True
finally:
    after = {str(path): receipts.sha(path) for path in inputs}
    receipts.save(output / 'inputs_after.json', after)
    receipts.save(output / 'verification.json', {'passed': passed and before == after,
        'contract_inputs_unchanged': before == after, 'input_count': len(inputs),
        'scope': 'Existing host-only inventory/freeze checks; no compilation, GUI, sysroot restoration or device access',
        'whole_application_frozen': False})
if before != after:
    raise RuntimeError('Contract inputs changed during validation')
