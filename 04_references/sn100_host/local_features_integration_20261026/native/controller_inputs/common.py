"""New-cohort evidence helpers. Import only defines APIs; no snapshot/build/probe runs."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import runpy
import signal
import subprocess
import time

WORK = Path(__file__).resolve().parent.parent
ROOT = WORK.parents[2]
PROGRAM = ROOT / '06_programs/sn100_host'
NATIVE = WORK / 'native'
FREEZE = NATIVE / 'final_before.json'
DRIVERS = WORK / 'cumulative_build'


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''):
            digest.update(block)
    return digest.hexdigest()


def now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def save(path, value):
    path = Path(path)
    temporary = path.with_name(path.name + '.writing')
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    temporary.replace(path)


def load(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def verify_controller_inputs():
    manifest = load(Path(__file__).with_name('preparation.json'))
    rows = manifest['controller_inputs']
    require(isinstance(rows, dict) and rows, 'Prepared controller inputs required')
    actual = {}
    for relative, expected in rows.items():
        path = (Path(__file__).parent / relative).resolve(strict=True)
        require(path.is_relative_to(Path(__file__).parent.resolve()), 'Controller input escaped preparation')
        actual[relative] = sha(path)
        require(actual[relative] == expected, 'Controller changed; root must review/reprepare: ' + relative)
    return actual


def verify_frozen(frozen):
    require(isinstance(frozen, dict) and frozen, 'Nonempty NEW formal source freeze required')
    mandatory = ('quality_control/qc_store.cpp', 'quality_control/quality_control_page.cpp',
        'calibration/calibration_store.cpp', 'calibration/calibration_page.cpp',
        'data/hospital_dictionary_query.h', 'history/report_dictionary_dialog.cpp',
        'history/report_dictionary_resources.qrc', 'ui/language_setting_page.cpp',
        'ui/frmlanguagesetting.ui', 'alarm_history/alarm_history_store.cpp',
        'alarm_history/alarm_history_page.cpp', 'alarm_history/alarm_history_host.cpp',
        'alarm_history/calendar/cuscalendar.cpp', 'alarm_history/alarm_history_resources.qrc',
        'shell/alarm_shell.cpp', 'shell/language_shell.cpp')
    require(set(mandatory).issubset(frozen), 'A new local-feature source is absent from the freeze')
    inventory = runpy.run_path(str(PROGRAM / 'shell/package_arm64.py'), run_name='sn100_new_local_inventory')
    current = inventory['snapshot'](PROGRAM)
    require(set(current) == set(frozen) and current == frozen,
            'Actual formal source set/SHA changed after the NEW freeze')


def verify_native(frozen):
    verify_frozen(frozen)
    report = load(NATIVE / 'verification.json')
    require(report.get('schema') == 'sn100-local-features-native-release-v1'
            and report.get('passed') is True and report.get('actual_exit') == 0,
            'Successful fresh native receipt from this NEW cohort required')
    require(report.get('source_inputs') == len(frozen) and report.get('snapshot_sha256') == sha(FREEZE),
            'Native receipt belongs to another source set')
    build = NATIVE / 'wsl_release'
    require(report.get('makefile_sha256') == sha(build / 'Makefile'), 'Native Makefile changed')
    for name in ('sn100_shell', 'sn100_report_renderer'):
        require(report['artifacts'].get(name) == sha(build / name), 'Native artifact changed: ' + name)
    require(isinstance(report.get('fresh_objects'), dict) and report['fresh_objects'], 'Dynamic fresh-object inventory missing')
    for relative, row in report['fresh_objects'].items():
        path = (NATIVE / relative).resolve(strict=True)
        require(path.is_relative_to(NATIVE.resolve()) and row.get('created_after_start') is True
                and sha(path) == row['sha256'], 'Native object is outside/currently differs: ' + relative)
    return report


def group_state(pgid):
    row = {'owned_process_group': pgid, 'gone': False}
    try:
        os.killpg(pgid, 0)
    except ProcessLookupError:
        row['gone'] = True
    except OSError as error:
        row['error'] = repr(error)
    return row


def stop_owned_group(process):
    """Bounded cleanup of the new session this call owns, independent of leader exit."""
    cleanup = {'owned_process_group': process.pid, 'signals': [], 'errors': []}
    for sig, budget in ((signal.SIGTERM, 3), (signal.SIGKILL, 2)):
        if group_state(process.pid)['gone']:
            break
        try:
            os.killpg(process.pid, sig)
            cleanup['signals'].append(sig.name)
        except ProcessLookupError:
            break
        except OSError as error:
            cleanup['errors'].append(repr(error))
            break
        deadline = time.monotonic() + budget
        while time.monotonic() < deadline:
            process.poll()
            if group_state(process.pid)['gone']:
                break
            time.sleep(.05)
    try:
        process.wait(timeout=.5)
    except subprocess.TimeoutExpired:
        cleanup['errors'].append('Direct child not reaped within final0.5s bound')
    cleanup['group_after'] = group_state(process.pid)
    cleanup['actual_exit'] = process.returncode
    return cleanup


def run_logged(name, command, output, env=None, timeout=120):
    output = Path(output)
    require(output.is_dir() and timeout is not None and timeout > 0, 'Existing output and bounded phase required')
    record = {'name': name, 'command': list(map(str, command)), 'cwd': str(ROOT),
              'started_utc': now(), 'stdout_stderr': name + '.log', 'timeout_seconds': timeout}
    save(output / (name + '.command.json'), record)
    process = None
    failure = None
    cleanup = None
    timed_out = False
    started = time.monotonic()
    try:
        with (output / (name + '.log')).open('xb') as stream:
            process = subprocess.Popen(record['command'], cwd=ROOT, env=env, start_new_session=True,
                                       stdout=stream, stderr=subprocess.STDOUT)
            save(output / (name + '.process.json'), {'pid': process.pid, 'state': 'running', 'new_session': True})
            process.wait(timeout=timeout)
            # Actual direct-child return is saved BEFORE cleanup/hash/parser checks.
            save(output / (name + '.raw_exit.json'), {'pid': process.pid, 'exit_code': process.returncode,
                 'observed_utc': now(), 'source': 'actual Popen.wait return', 'cleanup_not_yet_run': True})
    except BaseException as error:
        failure = error
        timed_out = isinstance(error, subprocess.TimeoutExpired)
    # Check the owned group even when its direct leader already returned.
    if process is not None:
        before = group_state(process.pid)
        if not before['gone']:
            try:
                cleanup = stop_owned_group(process)
            except BaseException as error:
                # Preserve the actual direct return even if cleanup itself failed.
                cleanup = {'owned_process_group':process.pid, 'signals':[],
                           'errors':['cleanup exception: ' + repr(error)],
                           'group_after':group_state(process.pid), 'actual_exit':process.returncode}
        stopped = group_state(process.pid)
    else:
        stopped = {'gone': False, 'not_started': True}
    code = process.returncode if process is not None else None
    record.update(exit_code=code, timed_out=timed_out,
                  state='not_started' if process is None else 'finished' if code is not None else 'exit_unknown',
                  pid=process.pid if process is not None else None, cleanup=cleanup,
                  owned_process_group_stopped=stopped['gone'], group_after=stopped,
                  cleanup_errors=cleanup['errors'] if cleanup else [],
                  seconds=time.monotonic()-started, finished_utc=now())
    if failure:
        record.update(exception_type=type(failure).__name__, exception=str(failure))
    save(output / (name + '.exit.json'), record)
    (output / (name + '.exit')).write_text(str(code) + '\n', encoding='ascii')
    save(output / (name + '.process.json'), {'pid': record['pid'], 'state': record['state'], 'exit_code': code})
    if failure:
        raise failure
    require(code == 0 and not timed_out and stopped['gone'] and not record['cleanup_errors']
            and not (cleanup and cleanup['signals']),
            name + ' failed/leaked/needed cleanup; original command, exit and log retained')
    return record


def private_environment(stage, qpa):
    """Only host-config/tmp directories; never precreate a test's runtime root here."""
    env = dict(os.environ)
    removed = []
    for key in tuple(env):
        if key.startswith(('SN100_', 'QT_', 'QMAKE', 'QML', 'CCACHE')) or key in (
                'LD_PRELOAD','LD_LIBRARY_PATH','MAKEFLAGS','MFLAGS','GNUMAKEFLAGS','MAKELEVEL'):
            removed.append(key)
            env.pop(key)
    host = Path(stage) / 'host_environment'
    host.mkdir()
    overrides = {'QT_QPA_PLATFORM':qpa, 'QT_SCALE_FACTOR':'1', 'QT_AUTO_SCREEN_SCALE_FACTOR':'0'}
    for key, suffix in (('XDG_RUNTIME_DIR','xdg'),('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),
                         ('XDG_CACHE_HOME','cache'),('TMPDIR','tmp')):
        path = host / suffix
        path.mkdir(mode=0o700)
        overrides[key] = str(path)
    overrides['TMP'] = overrides['TEMP'] = overrides['TMPDIR']
    env.update(overrides)
    return env, {'overrides':overrides, 'removed_inherited_names':sorted(removed),
                 'full_inherited_environment_not_recorded':True}
