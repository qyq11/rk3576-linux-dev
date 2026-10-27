#!/usr/bin/env python3
"""Root-authorized serial post-platform stage. Preparation never executes this file."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import time

from common import (WORK, load, save, sha, now, require, verify_native, FREEZE, NATIVE,
                    clean_environment, group_state, process_identity, cleanup_recorded_groups,
                    cleanup_ephemeral_inputs, stop_owned_group)

STAGES = {'standalone': ('run_standalone.py', 2400), 'projects': ('run_projects_modules.py', 7200),
          'response': ('run_response.py', 180), 'resource': ('run_resource.py', 100)}


def bound_state(native):
    return {'verified_actual_source_sha256': load(FREEZE), 'source_snapshot_sha256': sha(FREEZE),
            'verified_actual_native_objects_sha256': {name: row['sha256'] for name, row in native['fresh_objects'].items()},
            'verified_actual_artifacts': native['artifacts'], 'makefile_sha256': native['makefile_sha256'],
            'native_receipt_sha256': sha(NATIVE / 'verification.json'),
            'preparation_sha256': sha(Path(__file__).with_name('preparation.json'))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root-go', action='store_true', required=True)
    parser.add_argument('--stage', choices=tuple(STAGES), required=True)
    parser.add_argument('--name', required=True)
    args = parser.parse_args()
    import pwd
    require(args.root_go and os.name == 'posix' and os.geteuid() != 0
            and pwd.getpwuid(os.geteuid()).pw_name == 'qyq'
            and os.environ.get('WSL_DISTRO_NAME') == 'Ubuntu-24.04', 'Ubuntu-24.04/qyq and explicit slot required')
    require(re.fullmatch(r'[A-Za-z0-9_-]+', args.name), 'Simple fresh attempt name required')
    script, timeout = STAGES[args.stage]
    out = WORK / args.name
    destinations = [out, WORK / (args.name + '_control')]
    if args.stage == 'resource':
        destinations += [WORK / (args.name + suffix) for suffix in ('_runtime', '_driver')]
    require(not any(path.exists() or path.is_symlink() for path in destinations),
            'All output/control/runtime/driver paths must be fresh; use a new name after failure')
    native = verify_native(load(FREEZE))
    control = destinations[1]
    control.mkdir(mode=0o700)
    binding_before = bound_state(native)
    save(control / 'binding_before.json', binding_before)
    ledger = control / 'owned_groups.json'
    save(ledger, [])
    runtime = control / 'runtime'
    runtime.mkdir(mode=0o700)
    env, env_receipt = clean_environment(runtime, 'xcb')
    env['SN100_POST_LEDGER'] = str(ledger)
    save(control / 'environment.json', env_receipt)
    command = ['python3', '-B', str(Path(__file__).with_name(script)), '--root-go', '--name', args.name]
    record = {'schema': 'sn100-post-platform-controller-v1', 'stage': args.stage, 'command': command,
              'cwd': str(WORK), 'timeout_seconds': timeout, 'started_utc': now(),
              'state': 'not_started', 'actual_stage_exit': None, 'timed_out': False,
              'source_inputs': 447, 'native_fresh_objects': 210,
              'source_snapshot_sha256': native['snapshot_sha256'], 'passed': False}
    save(control / 'stage.command.json', record)
    process, failure = None, None
    try:
        with (control / 'stage.log').open('wb') as stream:
            process = subprocess.Popen(command, cwd=WORK, env=env, start_new_session=True,
                                       stdout=stream, stderr=subprocess.STDOUT)
            record.update(process_identity(process.pid), state='running')
            save(control / 'stage.process.json', record)
            try:
                process.wait(timeout=timeout)
            except BaseException as error:
                failure = error
                record['timed_out'] = isinstance(error, subprocess.TimeoutExpired)
                record['outer_cleanup'] = stop_owned_group(process)
        # Write the real stage exit before any group/assertion/source verification.
        record.update(actual_stage_exit=process.returncode, state='finished', finished_utc=now())
        save(control / 'stage.exit.json', record)
        (control / 'stage.exit').write_text(str(process.returncode) + '\n', encoding='ascii')
        record['outer_group_after'] = group_state(process.pid)
        record['recorded_group_cleanup'] = cleanup_recorded_groups(ledger, clean_live=True)
        record['ephemeral_inputs'] = cleanup_ephemeral_inputs(ledger, remove_if_found=False)
        require(failure is None and not record['timed_out'] and process.returncode == 0,
                'Stage failed/timed out; real exit retained, retry needs new name')
        require(record['outer_group_after']['gone'] and record['recorded_group_cleanup']['all_gone']
                and not record['recorded_group_cleanup']['signals_used'],
                'Normal completion requires every owned group already gone without forced cleanup')
        require(len(record['recorded_group_cleanup']['groups']) ==
                {'standalone': 7, 'projects': 18, 'response': 1, 'resource': 1}[args.stage],
                'Expected owned phase count missing from actual launch ledger')
        require(record['ephemeral_inputs']['all_absent'], 'Owned ephemeral input was not unlinked')
        binding_after = bound_state(verify_native(load(FREEZE)))
        save(control / 'binding_after.json', binding_after)
        require(binding_after == binding_before, 'Source/native/preparation binding changed during stage')
        record['passed'] = True
    except BaseException as error:
        record.update(error_type=type(error).__name__, error=str(error), passed=False)
        if process is not None:
            record['actual_stage_exit'] = process.returncode
            record['outer_group_after'] = group_state(process.pid)
        record['recorded_group_cleanup'] = cleanup_recorded_groups(ledger, clean_live=True)
        record['ephemeral_inputs'] = cleanup_ephemeral_inputs(ledger, remove_if_found=True)
        raise
    finally:
        record['finished_utc'] = now()
        save(control / 'verification.json', record)
    print({'passed': True, 'stage': args.stage, 'actual_stage_exit': record['actual_stage_exit'],
           'all_owned_groups_gone': True, 'verification': str(control / 'verification.json')}, flush=True)


if __name__ == '__main__':
    main()
