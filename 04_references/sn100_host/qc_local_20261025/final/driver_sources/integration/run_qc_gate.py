import argparse
import importlib.util
import os
import shutil
from pathlib import Path
from common import ROOT, WORK, PROGRAM, load, save, sha, run_logged

parser = argparse.ArgumentParser()
parser.add_argument('name')
parser.add_argument('--qpa', choices=('offscreen', 'xcb'), default='offscreen')
args = parser.parse_args()
out = WORK / args.name
out.mkdir(exist_ok=False)
spec = importlib.util.spec_from_file_location('qc_gate_package', PROGRAM / 'shell/package_arm64.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)
before = package.snapshot(PROGRAM)
save(out / 'source_before.json', before)
inputs = out / 'input_code'
inputs.mkdir()
for name in ('tests/qc_local_page_test.cpp', 'tests/qc_local_page_test.pro',
             'quality_control/quality_control_page.cpp', 'quality_control/quality_control_page.h',
             'quality_control/qc_store.cpp', 'quality_control/qc_store.h'):
    target = inputs / name
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(PROGRAM / name, target)
build = WORK / 'page_test_build'
command = ['make', '-C', str(build), '-j2']
run_logged('incremental_build', command, out)
binary = build / 'qc_local_page_test'
env = dict(os.environ, QT_QPA_PLATFORM=args.qpa, QT_SCALE_FACTOR='1', QT_AUTO_SCREEN_SCALE_FACTOR='0',
           SN100_QC_TEST_ROOT=str(out / 'runtime'), SN100_QC_EVIDENCE=str(out / 'evidence'))
save(out / 'environment.json', {key: env[key] for key in env if key.startswith('SN100_QC_') or key.startswith('QT_')})
save(out / 'driver.json', {'path': str(binary), 'sha256': sha(binary),
     'test_source_sha256': sha(PROGRAM / 'tests/qc_local_page_test.cpp')})
run_logged('qt_test', [str(binary), '-v1', '-o', str(out / 'qt.txt') + ',txt', '-o', str(out / 'qt.xml') + ',junitxml'], out, env, 100)
after = package.snapshot(PROGRAM)
save(out / 'source_after.json', after)
assert before == after
save(out / 'verification.json', {'source_unchanged': True, 'actual_exit': 0,
     'scope': 'Independent QC page business gate using actual project schema; not final unified/platform or resource acceptance'})
