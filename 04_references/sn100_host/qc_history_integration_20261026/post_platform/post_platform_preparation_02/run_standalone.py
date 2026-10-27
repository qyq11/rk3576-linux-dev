#!/usr/bin/env python3
"""Fresh standalone QC/calibration Release build and ordinary-close receipts.

Preparation does not execute this script. Root must explicitly pass --root-go
after the matching native build is complete and other heavy work has stopped.
Every runtime is newly generated anonymous data; no hand-made catalog schema,
production database/configuration, runtime hashing or credential archive is used.
"""
import argparse
import os
import re
from pathlib import Path
import secrets
import signal
import subprocess
import tempfile
import time

from common import (WORK, ROOT, PROGRAM, NATIVE, FREEZE, load, now, require,
                    save, sha, verify_frozen, verify_native, stop_owned_group)


from common import spawn_owned, clean_environment
from build_verification import verify_build, build_inventory

STAGE = WORK / 'post_standalone_02'


def run_phase(name, command, cwd, logs, env, timeout):
    """Record exact argv/cwd and real exit before examining or asserting output."""
    record = {'name': name, 'command': list(map(str, command)), 'cwd': str(cwd),
              'started_utc': now(), 'stdout_stderr': name + '.log'}
    save(logs / (name + '.command.json'), record)
    print('[standalone] start ' + name, flush=True)
    start = time.monotonic()
    process = None
    failure = None
    cleanup = None
    try:
        with (logs / (name + '.log')).open('wb') as stream:
            process = spawn_owned(record['command'], cwd=cwd, env=env,
                                       start_new_session=True, stdout=stream, stderr=subprocess.STDOUT)
            save(logs / (name + '.process.json'), {'pid': process.pid, 'state': 'running'})
            process.wait(timeout=timeout)
    except BaseException as error:
        failure = error
        if process is not None:
            cleanup = stop_owned_group(process)
    record.update(exit_code=process.returncode if process is not None else None,
                  timed_out=isinstance(failure, subprocess.TimeoutExpired),
                  state='not_started' if process is None else 'finished',
                  seconds=time.monotonic() - start, finished_utc=now(),
                  pid=process.pid if process is not None else None, cleanup=cleanup,
                  exit_receipt='logs/' + name + '.exit.json', log='logs/' + name + '.log')
    if failure:
        record.update(exception_type=type(failure).__name__, exception=str(failure))
    save(logs / (name + '.exit.json'), record)
    (logs / (name + '.exit')).write_text(str(record['exit_code']) + '\n', encoding='ascii')
    save(logs / (name + '.process.json'), {'pid': record['pid'], 'state': record['state'],
                                         'exit_code': record['exit_code']})
    if failure:
        raise failure
    require(record['exit_code'] == 0, name + ' failed; original command/exit/log retained')
    print('[standalone] finished ' + name + ' exit=' + str(record['exit_code']), flush=True)
    return record


def create_catalog_with_real_owners(command, logs, env):
    """Start the exact native shell; await real readiness, then ordinary SIGTERM."""
    name = 'calibration_catalog_owner'
    record = {'name': name, 'command': list(map(str, command)), 'cwd': str(ROOT),
              'started_utc': now(), 'stdout_stderr': name + '.log',
              'ready_marker': 'SHELL_READY', 'readiness_observed': False,
              'ordinary_sigterm_requested': False}
    save(logs / (name + '.command.json'), record)
    print('[standalone] start ' + name, flush=True)
    start = time.monotonic()
    process = None
    failure = None
    cleanup = None
    path = logs / (name + '.log')
    try:
        with path.open('wb') as stream:
            process = spawn_owned(record['command'], cwd=ROOT, env=env,
                                       start_new_session=True, stdout=stream, stderr=subprocess.STDOUT)
            save(logs / (name + '.process.json'), {'pid': process.pid, 'state': 'running'})
            deadline = time.monotonic() + 20
            while process.poll() is None and time.monotonic() < deadline:
                text = path.read_text(encoding='utf-8', errors='replace')
                if 'SHELL_READY' in text.splitlines():
                    record['readiness_observed'] = True
                    record['ready_seconds'] = time.monotonic() - start
                    break
                time.sleep(0.05)
            if not record['readiness_observed']:
                raise RuntimeError('Real catalog owners did not report SHELL_READY before exit/readiness timeout')
            process.send_signal(signal.SIGTERM)
            record['ordinary_sigterm_requested'] = True
            process.wait(timeout=15)
    except BaseException as error:
        failure = error
        if process is not None:
            cleanup = stop_owned_group(process)
    record.update(exit_code=process.returncode if process is not None else None,
                  timed_out=isinstance(failure, subprocess.TimeoutExpired),
                  state='not_started' if process is None else 'finished',
                  seconds=time.monotonic() - start, finished_utc=now(),
                  pid=process.pid if process is not None else None, cleanup=cleanup,
                  exit_receipt='logs/' + name + '.exit.json', log='logs/' + name + '.log')
    if failure:
        record.update(exception_type=type(failure).__name__, exception=str(failure))
    # Preserve the original child exit before checking ready/close marker counts.
    save(logs / (name + '.exit.json'), record)
    (logs / (name + '.exit')).write_text(str(record['exit_code']) + '\n', encoding='ascii')
    save(logs / (name + '.process.json'), {'pid': record['pid'], 'state': record['state'],
                                         'exit_code': record['exit_code']})
    if failure:
        raise failure
    require(record['exit_code'] == 0, 'Real catalog owner did not exit normally; raw exit retained')
    text = path.read_text(encoding='utf-8')
    closed = ['HOSPITAL_STORE_CLOSED', 'ACCOUNT_STORE_CLOSED', 'PROJECT_STORE_CLOSED',
              'REAGENT_STORE_CLOSED', 'SAMPLE_STORE_CLOSED']
    record['checks'] = {'ready_once': text.splitlines().count('SHELL_READY') == 1,
                        'five_owner_workers_closed_once': all(text.count(marker) == 1 for marker in closed),
                        'all_threads_stopped': 'THREADS_STOPPED true' in text,
                        'ordinary_app_exit_marker': 'EXIT code= 0' in text}
    record['closure_marker_counts'] = {marker: text.count(marker) for marker in closed}
    save(logs / (name + '.verification.json'), record)
    require(all(record['checks'].values()), 'Owner close/readiness markers failed; real exit already recorded')
    print('[standalone] finished ' + name + ' exit=' + str(record['exit_code']), flush=True)
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root-go', action='store_true', required=True)
    parser.add_argument('--name', default='post_standalone_02')
    args = parser.parse_args()
    global STAGE
    require(re.fullmatch(r'[A-Za-z0-9_-]+', args.name), 'Simple fresh attempt name required')
    STAGE = WORK / args.name
    require(args.root_go and os.geteuid() != 0, 'Explicit --root-go and ordinary WSL user required')
    require(not STAGE.exists(), 'Fresh standalone_final directory required; do not overwrite prior attempts')
    frozen = load(FREEZE)
    native = verify_native(frozen)
    main_artifacts = dict(native['artifacts'])
    native_verification_sha = sha(NATIVE / 'verification.json')
    source_before = {name: sha(PROGRAM / name) for name in frozen}
    require(source_before == frozen, 'Actual standalone inputs do not match successful native freeze')
    STAGE.mkdir()
    logs = STAGE / 'logs'
    runtime = STAGE / 'runtime'
    logs.mkdir()
    runtime.mkdir()
    save(STAGE / 'source_before.json', source_before)
    save(STAGE / 'native_binding_before.json', {
        'source_snapshot_sha256': sha(FREEZE), 'source_inputs': len(frozen),
        'native_verification_sha256': native_verification_sha, 'main_artifacts': main_artifacts})
    selected_env = {'QT_QPA_PLATFORM': 'xcb', 'QT_SCALE_FACTOR': '1',
                    'QT_AUTO_SCREEN_SCALE_FACTOR': '0'}
    save(STAGE / 'environment.json', {'environment_overrides': selected_env,
         'parallel_jobs_maximum': 2, 'build': 'Release', 'runtime_values_hashed_or_archived': False,
         'display': os.environ.get('DISPLAY'), 'wayland_display': os.environ.get('WAYLAND_DISPLAY')})
    build_env, clean_receipt = clean_environment(runtime, 'xcb')
    gui_env = dict(build_env)
    save(STAGE / 'private_environment.json', clean_receipt)
    stages = {}
    modules = {}
    module_inventories = {}
    secret = None
    try:
        for name, relative, target in (
                ('quality_control', 'quality_control/quality_control.pro', 'sn100_qc_local'),
                ('calibration', 'calibration/calibration.pro', 'sn100_calibration')):
            build_dir = STAGE / 'build' / name
            build_dir.mkdir(parents=True)
            build_started_ns = time.time_ns()
            qmake_name, make_name = name + '_qmake', name + '_make'
            stages[qmake_name] = run_phase(qmake_name, ['/usr/lib/qt5/bin/qmake', str(PROGRAM / relative),
                '-spec', 'linux-g++', 'CONFIG+=release', 'CONFIG-=debug'], build_dir, logs, build_env, 120)
            stages[make_name] = run_phase(make_name, ['make', '-j2'], build_dir, logs, build_env, 900)
            binary = build_dir / target
            require(binary.is_file(), 'Successful make did not create ' + target)
            inventory, proof = verify_build(build_dir, binary, frozen, build_started_ns)
            module_inventories[name] = inventory
            save(logs / (name + '_build_inventory.json'), inventory)
            save(logs / (name + '_build_proof.json'), proof)
            modules[name] = {'project': relative, 'target': target,
                'binary': binary.relative_to(STAGE).as_posix(), 'binary_sha256': sha(binary),
                'makefile_sha256': sha(build_dir / 'Makefile'),
                'qmake_stage': qmake_name, 'make_stage': make_name, 'run_stage': name + '_run'}

        qc_binary = STAGE / modules['quality_control']['binary']
        qc_command = [str(qc_binary), '--data-dir', str(runtime / 'quality_control'),
                      '--seed-demo', '--exit-after-ms', '1800']
        stages['quality_control_run'] = run_phase('quality_control_run', qc_command, ROOT, logs, gui_env, 30)
        qc_text = (logs / 'quality_control_run.log').read_text(encoding='utf-8')
        qc_marker = '质控元数据已读取；所选项目历史另行只读加载，临床规则未启用。'
        modules['quality_control'].update(startup_marker=qc_marker,
            startup_marker_observed=qc_marker in qc_text,
            normal_shutdown_basis='Actual standalone process exit0 after --exit-after-ms1800; frozen main.cpp closes both worker stores with BlockingQueuedConnection and thread.quit/wait before returning. No independent shutdown log marker exists.')
        require(modules['quality_control']['startup_marker_observed'], 'QC standalone did not report a real loaded page')

        owner_runtime = runtime / 'calibration_catalog_owner'
        descriptor, filename = tempfile.mkstemp(prefix='.catalog-owner-bootstrap-', dir=runtime)
        secret = Path(filename)
        try:
            os.fchmod(descriptor, 0o600)
            os.write(descriptor, secrets.token_hex(24).encode('ascii'))
        finally:
            os.close(descriptor)
        owner_command = [str(NATIVE / 'wsl_release/sn100_shell'), '--offline', '--accounts', '--catalog',
            '--samples', '--seed-catalog-demo', '--seed-anonymous', '--test-secret-file', str(secret),
            '--data-dir', str(owner_runtime), '--viewport', '1600x900']
        try:
            stages['calibration_catalog_owner'] = create_catalog_with_real_owners(owner_command, logs, gui_env)
        finally:
            secret.unlink(missing_ok=True)
        catalog = owner_runtime / 'catalog/catalog.db'
        require(catalog.is_file(), 'Real native catalog owners did not create their catalog.db')
        cal_binary = STAGE / modules['calibration']['binary']
        cal_command = [str(cal_binary), '--data-dir', str(runtime / 'calibration_local'),
                       '--catalog-db', str(catalog), '--exit-after-ms', '1800']
        stages['calibration_run'] = run_phase('calibration_run', cal_command, ROOT, logs, gui_env, 30)
        cal_text = (logs / 'calibration_run.log').read_text(encoding='utf-8')
        cal_marker = '已读取本地原始点记录；全部未审核、未应用。右键曲线列表可新增。'
        cal_close = 'calibration normal worker/database shutdown, exit 0'
        modules['calibration'].update(startup_marker=cal_marker,
            startup_marker_observed=cal_marker in cal_text,
            normal_shutdown_marker=cal_close, normal_shutdown_marker_observed=cal_close in cal_text,
            normal_shutdown_basis='Actual --exit-after-ms1800 invokes CalibrationHost close guard; main closes its store and joins worker before emitting its explicit shutdown marker and returning.',
            catalog_source='Real matching native sn100_shell Project/Reagent/Sample owner initialization after SHELL_READY, not Python-created schema. Calibration mandatory guard may append its own path binding/reservations only; no project/reagent values are edited by this standalone run.')

        source_after = {name: sha(PROGRAM / name) for name in frozen}
        save(STAGE / 'source_after.json', source_after)
        verify_frozen(frozen)
        native_after = verify_native(frozen)
        binding_after = {'source_snapshot_sha256': sha(FREEZE), 'source_inputs': len(frozen),
            'native_verification_sha256': sha(NATIVE / 'verification.json'),
            'main_artifacts': dict(native_after['artifacts'])}
        save(STAGE / 'native_binding_after.json', binding_after)
        checks = {'fresh_standalone_objects_unchanged': all(build_inventory(STAGE / row['binary'].rsplit('/',1)[0], STAGE / row['binary']) == module_inventories[name] for name, row in modules.items()),
            'source_unchanged': source_before == source_after == frozen,
            'main_artifacts_unchanged': main_artifacts == binding_after['main_artifacts'],
            'native_verification_unchanged': native_verification_sha == binding_after['native_verification_sha256'],
            'all_actual_phase_exits0': len(stages) == 7 and all(row['exit_code'] == 0 for row in stages.values()),
            'qc_real_page_loaded': modules['quality_control']['startup_marker_observed'],
            'cal_real_page_loaded': modules['calibration']['startup_marker_observed'],
            'cal_normal_shutdown_marker': modules['calibration']['normal_shutdown_marker_observed'],
            'real_catalog_owner_ready_closed': all(stages['calibration_catalog_owner']['checks'].values()),
            'bootstrap_unlinked': secret is not None and not secret.exists(),
            'standalone_binaries_unchanged': all(sha(STAGE / row['binary']) == row['binary_sha256'] for row in modules.values())}
        result = {'schema': 'sn100-qc-calibration-standalone-fresh-v2', 'passed': all(checks.values()),
            'standalone_app_count': 2, 'ordinary_app_names': ['quality_control', 'calibration'],
            'total_phase_count': 7, 'old01_build_artifacts_reused': False,
            'finished_utc': now(), 'source_snapshot_sha256': sha(FREEZE), 'source_inputs': len(frozen),
            'native_verification_sha256': native_verification_sha, 'main_artifacts': main_artifacts,
            'source_before': source_before, 'source_after': source_after,
            'stages': stages, 'modules': modules, 'checks': checks,
            'runtime_data_ini_or_credentials_archived': False,
            'scope': 'Fresh WSL native standalone qmake Release builds plus real anonymous page startup/1800ms normal close. No CRUD/clinical/RK/Windows/runtime-budget qualification; those require separate evidence.'}
        save(STAGE / 'verification.json', result)
        require(result['passed'], 'Standalone final checks failed; original commands/logs/exits retained')
        print({'passed': result['passed'], 'source_inputs': len(frozen), 'checks': checks}, flush=True)
    except BaseException as error:
        save(STAGE / 'failure.json', {'error_type': type(error).__name__, 'error': str(error),
            'failed_utc': now(), 'actual_stages_completed': stages, 'old_outputs_preserved': True,
            'runtime_data_ini_or_credentials_archived': False})
        raise
    finally:
        if secret is not None:
            secret.unlink(missing_ok=True)


if __name__ == '__main__':
    main()
