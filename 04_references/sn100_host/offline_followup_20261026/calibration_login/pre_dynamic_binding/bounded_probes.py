#!/usr/bin/env python3
"""Prepare private probes; execute only after the integrator grants a serial slot."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import runpy
import signal
import subprocess
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
PROGRAM = ROOT / '06_programs/sn100_host'
COHORT = ROOT / 'temp/sn100_host/qc_local_20261025'
PREIMAGE = ROOT / '07_backups/sn100_host/offline_four_windows_20261025/review_test_preimage'
EXPECTED_FREEZE = '5123bf1442472450bbde23a59fc47287dc85ead34e006c523a96a016ac76cf29'
SOURCES = {
    'calibration_old': ('calibration_reference/calibration_reference_test.cpp',
                        '5b8cfa9653218b193f5b1f916f651a863ed23ab056fa2328ada128094f48e155'),
    'calibration_current': ('tests/calibration_reference_test.cpp',
                            '25c984c73a5b0d7486edb87aacfea8d21ac4c4f691f99c8dc082adfa883b7239'),
    'login_old': ('login_geometry/password_login_shell_test.cpp',
                  '5accedc33d663270819339e05b1f0323c44cc7a0d1a1d032a9feea0420d28d8f'),
    'login_current': ('tests/password_login_shell_test.cpp',
                      '8374aeea26e9a670a7fc27c88538db615ad31cf02c3bc19ecbc6d95ce5984afe'),
}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def save(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def assertions(text):
    return [line.strip() for line in text.splitlines()
            if re.match(r'\s*(?:QVERIFY2?|QCOMPARE|QTRY_VERIFY)\s*\(', line)]


def login_probe(text):
    text = '#include "../login_geometry_probe.h"\n' + text
    anchors = [
        ('    Session session(QDir(runtime_).filePath("legacy_guest"));',
         '    Session session(QDir(runtime_).filePath("legacy_guest"));\n'
         '    LoginGeometryProbe geometryProbe(session.host);'),
        ('    for (const QSize size : {QSize(1600, 900), QSize(1920, 1080)})\n    {',
         '    for (const QSize size : {QSize(1600, 900), QSize(1920, 1080)})\n    {\n'
         '        geometryProbe.prepareTransition(size);'),
        ('        QTest::mouseClick(user, Qt::LeftButton);',
         '        geometryProbe.mark("before_avatar_click");\n'
         '        QTest::mouseClick(user, Qt::LeftButton);\n'
         '        geometryProbe.mark("after_avatar_click");'),
        ('        QVERIFY(available.contains(cardRect));',
         '        geometryProbe.mark("before_unchanged_contains_assertion");\n'
         '        QVERIFY(available.contains(cardRect));'),
    ]
    for before, after in anchors:
        require(text.count(before) == 1, 'Unexpected login probe anchor: ' + before)
        text = text.replace(before, after)
    return text


def prepare():
    prepared = HERE / 'prepared'
    require(not prepared.exists(), 'Keep existing prepared inputs; do not overwrite them')
    source_rows = {}
    # Validate every input before creating the new preparation directory.
    for variant, (relative, expected) in SOURCES.items():
        origin = (PREIMAGE if variant.endswith('_old') else PROGRAM) / relative
        require(sha(origin) == expected, 'Source does not match reviewed preimage: ' + str(origin))
        source_rows[variant] = (origin, expected)
    prepared.mkdir()
    header = HERE / 'login_geometry_probe.h'
    (prepared / header.name).write_bytes(header.read_bytes())
    manifest = {'schema': 'sn100-calibration-login-private-probe-inputs-v1',
                'created_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
                'production_change': False, 'prepared_only': True,
                'header_sha256': sha(header), 'variants': {}}
    for variant, (origin, expected) in source_rows.items():
        destination = prepared / variant / origin.name
        destination.parent.mkdir()
        original_bytes = origin.read_bytes()
        original = original_bytes.decode('utf-8').replace('\r\n', '\n')
        probe = login_probe(original) if variant.startswith('login_') else original
        require(assertions(probe) == assertions(original), 'Original assertion lines changed')
        if variant.startswith('login_'):
            destination.write_text(probe, encoding='utf-8')
        else:
            destination.write_bytes(original_bytes)  # Exact old/current calibration source bytes.
        manifest['variants'][variant] = {
            'origin': str(origin), 'origin_sha256': expected, 'source': str(destination),
            'source_sha256': sha(destination), 'original_assertion_lines': len(assertions(original)),
            'original_assertion_lines_preserved': True,
            'instrumentation': 'buffered geometry only' if variant.startswith('login_') else 'no source changes',
        }
    save(prepared / 'inputs.json', manifest)
    print(json.dumps({'prepared': str(prepared), 'build_or_gui_started': False}))


def verify_inputs():
    require(os.geteuid() != 0, 'Ordinary WSL user required')
    manifest = load(HERE / 'prepared/inputs.json')
    require(sha(HERE / 'prepared/login_geometry_probe.h') == manifest['header_sha256'],
            'Prepared geometry header changed')
    for row in manifest['variants'].values():
        require(sha(row['source']) == row['source_sha256'] and sha(row['origin']) == row['origin_sha256'],
                'Reviewed source or prepared copy changed')
    freeze = COHORT / 'native/final_before.json'
    require(sha(freeze) == EXPECTED_FREEZE, 'Different native source cohort: request a new plan')
    snapshot = load(freeze)
    require(len(snapshot) == 426, 'Unexpected input count')
    for name, expected in snapshot.items():
        require(sha(PROGRAM / name) == expected, 'Formal source changed: ' + name)
    inventory = runpy.run_path(str(PROGRAM / 'shell/package_arm64.py'),
                              run_name='sn100_bounded_probe_inventory')
    require(inventory['snapshot'](PROGRAM) == snapshot, 'Formal input set changed')
    native = load(COHORT / 'native/verification.json')
    require(native['passed'] is True and native['actual_exit'] == 0
            and native['snapshot_sha256'] == EXPECTED_FREEZE, 'No successful exact-cohort native receipt')
    require(sha(COHORT / 'native/wsl_release/Makefile') == native['makefile_sha256'],
            'Native Makefile changed')
    for name, row in native['fresh_objects'].items():
        require(row['created_after_start'] is True
                and sha(COHORT / 'native' / name) == row['sha256'], 'Native object changed: ' + name)
    for name, expected in native['artifacts'].items():
        require(sha(COHORT / 'native/wsl_release' / name) == expected, 'Native binary changed')
    proof = load(COHORT / 'cumulative_build/binding_proof.json')
    require(proof['snapshot_sha256'] == EXPECTED_FREEZE
            and proof['native_makefile_sha256'] == native['makefile_sha256']
            and len(proof['native_objects']) == 167, 'Different test production binding')
    for name, row in proof['native_objects'].items():
        require(sha(name) == row['object_sha256'] and sha(row['source']) == row['source_sha256'],
                'Test production object/source changed: ' + name)
    return manifest, proof


def run_command(name, command, output, environment, limit):
    require(not (output / (name + '.exit.json')).exists(), 'Never overwrite an execution receipt')
    receipt = {'name': name, 'command': command, 'cwd': str(COHORT / 'native/wsl_release'),
               'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
               'timeout_seconds': limit, 'timed_out': False, 'exit_code': None,
               'cleanup_signals_requested': []}
    save(output / (name + '.command.json'), receipt)
    process = None
    failure = None
    started = time.monotonic()
    try:
        with (output / (name + '.log')).open('wb') as log:
            process = subprocess.Popen(command, cwd=receipt['cwd'], env=environment,
                                       start_new_session=True, stdout=log, stderr=subprocess.STDOUT)
            receipt['pid'] = process.pid
            process.wait(timeout=limit)
    except BaseException as error:
        failure = error
        receipt.update(exception=type(error).__name__, error=str(error),
                       timed_out=isinstance(error, subprocess.TimeoutExpired))
    finally:
        # Only this new process group; never select another chat's process by name.
        if process is not None:
            for kind in (signal.SIGTERM, signal.SIGKILL):
                try:
                    os.killpg(process.pid, kind)
                    receipt['cleanup_signals_requested'].append(int(kind))
                except ProcessLookupError:
                    pass
                if process.poll() is None:
                    try:
                        process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        pass
            receipt['exit_code'] = process.returncode
            try:
                os.killpg(process.pid, 0)
                receipt['owned_group_gone'] = False
            except ProcessLookupError:
                receipt['owned_group_gone'] = True
        receipt['elapsed_seconds'] = time.monotonic() - started
        save(output / (name + '.exit.json'), receipt)  # Before any result interpretation.
    if failure:
        raise failure
    require(process is not None and process.returncode is not None and receipt['owned_group_gone'],
            'Owned process group not confirmed stopped; keep failure and do not start another run')
    return receipt


def variant_commands(variant, build, manifest, proof):
    row = manifest['variants'][variant]
    stem = Path(row['source']).stem
    bound = proof['drivers'][stem]
    require(bound['source_sha256'] == SOURCES[
        'calibration_current' if stem.startswith('calibration') else 'login_current'][1],
        'Current driver does not bind the reviewed test source')
    recipes = [entry['command'] for entry in bound['receipts']]
    require(len(recipes) == 3 and all(entry['exit_code'] == 0 for entry in bound['receipts']),
            'Expected actual moc/compile/link recipes')
    old_build = Path(recipes[2][recipes[2].index('-o') + 1]).parent
    exact_source = str(PROGRAM / bound['source_relative'])
    commands = [[arg.replace(str(old_build), str(build)).replace(exact_source, row['source'])
                 for arg in recipe] for recipe in recipes]
    sanitizer = ['-g', '-fno-omit-frame-pointer', '-fsanitize=address,undefined']
    if variant.startswith('calibration'):
        # Only the test translation unit is instrumented; all 167 production
        # objects and Qt libraries remain Release/uninstrumented. Do not
        # interpret a clean run as a full-production memory-safety proof.
        commands[1] += sanitizer
        commands[2] += sanitizer
    return commands


def plan():
    destination = HERE / 'command_plan.json'
    require(not destination.exists(), 'Keep the existing reviewed command plan')
    manifest = load(HERE / 'prepared/inputs.json')
    proof = load(COHORT / 'cumulative_build/binding_proof.json')
    phases = {}
    for phase in ('calibration', 'login'):
        output = HERE / (phase + '_slot_01')
        variants = [phase + '_old', phase + '_current']
        phases[phase] = {
            'execute_argv': ['python3', '-B', str(Path(__file__).resolve()), 'execute',
                             '--phase', phase, '--attempt', output.name, '--slot-id', 'GRANTED_SLOT_ID'],
            'estimated_minutes': '3-5' if phase == 'calibration' else '2-4',
            'compiler_jobs': 1, 'GUI': phase == 'login',
            'test_limit_seconds': 60 if phase == 'calibration' else 45,
            'build_step_limit_seconds': 120,
            'commands': {variant: variant_commands(variant, output / variant, manifest, proof)
                         for variant in variants},
            'test_calls': (2 if phase == 'calibration' else 4),
            'test_selection': ('one full sequence per source' if phase == 'calibration'
                               else 'legacyOptInAndGuestLogin, natural + pending own-host move'),
        }
    save(destination, {'schema': 'sn100-private-probe-command-plan-v1', 'phases': phases,
         'planning_only': True, 'built_or_run': False, 'native_proof_sha256': sha(
             COHORT / 'cumulative_build/binding_proof.json'),
         'pending': 'No slot granted. Full source/object/binary validation is required at execution.'})
    print(json.dumps({'command_plan': str(destination), 'build_or_gui_started': False}))


def build_variant(variant, output, manifest, proof, environment):
    row = manifest['variants'][variant]
    stem = Path(row['source']).stem
    build = output / variant
    build.mkdir()
    (build / '.moc').mkdir()
    (build / '.obj').mkdir()
    commands = variant_commands(variant, build, manifest, proof)
    save(build / 'provenance.json', {'variant': variant, 'source': row,
         'formal_snapshot_sha256': EXPECTED_FREEZE, 'native_objects': proof['native_objects'],
         'native_objects_instrumented': False, 'test_only_sanitizers': variant.startswith('calibration'),
         'original_recipe_sha256': sha(COHORT / 'cumulative_build/binding_proof.json')})
    for name, command in zip(('moc', 'compile', 'link'), commands):
        result = run_command(name, command, build, environment, 120)
        require(result['exit_code'] == 0, 'Build failed: ' + str(build / (name + '.log')))
    binary = build / stem
    save(build / 'binary.json', {'path': str(binary), 'sha256': sha(binary)})
    return binary


def execute(args):
    require(args.slot_id and re.fullmatch(r'[A-Za-z0-9_-]+', args.slot_id),
            'An explicit granted serial slot ID is required')
    require(args.attempt and re.fullmatch(r'[A-Za-z0-9_-]+', args.attempt), 'Simple fresh attempt required')
    output = HERE / args.attempt
    require(not output.exists(), 'Keep every earlier attempt')
    output.mkdir()
    save(output / 'slot.json', {'slot_id': args.slot_id, 'phase': args.phase,
         'caller_attestation': 'Integrator granted this serial slot; no other GUI/build/measurement',
         'root_or_device_access': False})
    manifest, proof = verify_inputs()
    environment = dict(os.environ, QT_SCALE_FACTOR='1', QT_AUTO_SCREEN_SCALE_FACTOR='0',
                       SN100_WSL_IME='off', QT_QPA_PLATFORM='offscreen' if args.phase == 'calibration' else 'xcb')
    variants = (['calibration_old', 'calibration_current'] if args.phase == 'calibration'
                else ['login_old', 'login_current'])
    results = []
    post_verified = False
    post_error = None
    try:
        binaries = {name: build_variant(name, output, manifest, proof, environment) for name in variants}
        for variant in variants:
            for pending in ([False] if args.phase == 'calibration' else [False, True]):
                run = output / (variant + ('_pending_move' if pending else '_natural'))
                run.mkdir()
                env = dict(environment)
                env.pop('SN100_DIAG_PENDING_HOST_MOVE', None)
                if args.phase == 'calibration':
                    runtime = run / 'runtime'
                    runtime.mkdir()
                    env['SN100_REFERENCE_TEST_ROOT'] = str(runtime)
                    env['ASAN_OPTIONS'] = 'abort_on_error=1:detect_leaks=1:log_path=' + str(run / 'asan')
                    env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1:log_path=' + str(run / 'ubsan')
                    selectors = []  # One complete 22-result sequence, not retry-until-pass.
                else:
                    env.update(SN100_LOGIN_EVIDENCE_DIR=str(run / 'evidence'),
                               SN100_LOGIN_RUNTIME_DIR=str(run / 'runtime'),
                               SN100_SHELL_APP=str(COHORT / 'native/wsl_release/sn100_shell'),
                               SN100_SHELL_SHA256=sha(COHORT / 'native/wsl_release/sn100_shell'),
                               SN100_DIAG_GEOMETRY_TRACE=str(run / 'geometry.json'))
                    if pending:
                        env['SN100_DIAG_PENDING_HOST_MOVE'] = '1'
                    selectors = ['legacyOptInAndGuestLogin']
                result = run_command('test', [str(binaries[variant]), *selectors, '-v1',
                    '-o', str(run / 'qt.txt') + ',txt', '-o', str(run / 'qt.xml') + ',junitxml'],
                    run, env, 60 if args.phase == 'calibration' else 45)
                results.append({'variant': variant, 'pending_host_move': pending, 'receipt': result})
                if args.phase == 'calibration' and result['exit_code'] != 0:
                    raw = (run / 'test.log').read_text(errors='replace')
                    for path in run.glob('asan.*'):
                        raw += path.read_text(errors='replace')
                    unsupported = ('ASan runtime does not come first', 'Shadow memory range interleaves',
                                   'failed to mmap', 'error while loading shared libraries')
                    if 'Start testing of CalibrationReferenceTest' not in raw and any(
                            message in raw for message in unsupported):
                        results[-1]['sanitizer_startup_failure'] = True
                        raise RuntimeError('Sanitizer could not start; preserve failure, no repeat or system workaround')
    finally:
        try:
            verify_inputs()
            post_verified = True
        except Exception as error:
            post_error = str(error)
        save(output / 'results.json', {'phase': args.phase, 'runs': results,
             'inputs_unchanged_after': post_verified, 'post_verification_error': post_error,
             'original_failure_cause_proven': False,
             'interpretation': 'Raw bounded diagnostic outcomes only; inspect any sanitizer stack or geometry event mismatch. Passing repetitions do not establish the original cause.'})
    return 0 if post_verified and results and all(row['receipt']['exit_code'] == 0 for row in results) else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('prepare', 'plan', 'execute'))
    parser.add_argument('--phase', choices=('calibration', 'login'))
    parser.add_argument('--attempt')
    parser.add_argument('--slot-id')
    args = parser.parse_args()
    if args.mode == 'prepare':
        prepare()
        return 0
    if args.mode == 'plan':
        plan()
        return 0
    require(args.phase is not None, 'Choose one separately queued phase')
    return execute(args)


if __name__ == '__main__':
    raise SystemExit(main())

