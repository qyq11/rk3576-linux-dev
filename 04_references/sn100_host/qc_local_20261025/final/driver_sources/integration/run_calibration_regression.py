import argparse
import os
import shutil
from common import WORK, PROGRAM, sha, save, run_logged

parser = argparse.ArgumentParser()
parser.add_argument('name')
parser.add_argument('--mode', choices=('store', 'page'), default='store')
parser.add_argument('--qpa', choices=('offscreen', 'xcb'), default='offscreen')
args = parser.parse_args()
out = WORK / args.name
out.mkdir(exist_ok=False)
build = WORK / 'calibration_regression_build' / args.mode
build.mkdir(parents=True, exist_ok=True)
project = PROGRAM / 'calibration/tests' / ('calibration_' + args.mode + '_test.pro')
sources = ['calibration/calibration_store.cpp', 'calibration/calibration_store.h',
           'calibration/tests/calibration_fixture.h', 'calibration/tests/calibration_' + args.mode + '_test.cpp']
for name in sources:
    target = out / 'input_code' / name
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(PROGRAM / name, target)
save(out / 'inputs.json', {name: sha(PROGRAM / name) for name in sources})
run_logged('qmake', ['/usr/lib/qt5/bin/qmake', str(project), '-o', str(build / 'Makefile'),
           '-spec', 'linux-g++', 'CONFIG+=release', 'CONFIG-=debug'], out)
run_logged('make', ['make', '-C', str(build), '-j2'], out)
env = dict(os.environ, QT_QPA_PLATFORM=args.qpa, QT_SCALE_FACTOR='1', QT_AUTO_SCREEN_SCALE_FACTOR='0',
           CALIBRATION_TEST_ROOT=str(out / 'runtime'), CALIBRATION_SCREENSHOT_DIR=str(out / 'evidence'))
save(out / 'environment.json', {key: env[key] for key in env if key.startswith('CALIBRATION_') or key.startswith('QT_')})
binary = build / ('calibration_' + args.mode + '_test')
save(out / 'driver.json', {'path': str(binary), 'sha256': sha(binary)})
run_logged('qt_test', [str(binary), '-v1', '-o', str(out / 'qt.txt') + ',txt',
           '-o', str(out / 'qt.xml') + ',junitxml'], out, env, 180)
assert all(sha(PROGRAM / name) == sha(out / 'input_code' / name) for name in sources)
save(out / 'verification.json', {'actual_exit': 0, 'input_sources_unchanged': True,
     'scope': 'Post-reservation calibration regression; independent anonymous fixtures, not original Windows/RK/instrument acceptance'})
