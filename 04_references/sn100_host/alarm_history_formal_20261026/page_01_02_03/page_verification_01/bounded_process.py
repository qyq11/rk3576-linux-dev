import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import time

def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def save(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def exists(pid, group=False):
    try:
        (os.killpg if group else os.kill)(pid, 0)
        return True
    except ProcessLookupError:
        return False


def executable(path):
    found = shutil.which(str(path))
    require(found is not None, 'Existing executable unavailable: ' + str(path))
    entry = Path(found).absolute()
    resolved = entry.resolve(strict=True)
    return {'entry': str(entry), 'resolved': str(resolved), 'sha256': sha(resolved)}


def run(name, argv, out, cwd, environment, timeout, deadline):
    seconds = min(timeout, deadline - time.monotonic())
    require(seconds > 0, '180 second controller deadline reached before ' + name)
    receipt = {'argv': argv, 'cwd': str(cwd), 'started_utc': now(), 'timeout_seconds': seconds,
               'pid': None, 'exit_code': None, 'timed_out': False, 'cleanup_signals': []}
    save(out / (name + '.command.json'), receipt)
    started = time.monotonic()
    child = None
    failure = None
    try:
        with (out / (name + '.stdout.log')).open('wb') as stdout, \
                (out / (name + '.stderr.log')).open('wb') as stderr:
            child = subprocess.Popen(argv, cwd=cwd, env=environment, stdout=stdout, stderr=stderr,
                                     start_new_session=True)
            receipt['pid'] = child.pid
            save(out / (name + '.process.json'), {'pid': child.pid, 'state': 'running'})
            child.wait(timeout=seconds)
    except BaseException as error:
        failure = error
        receipt.update(exception=type(error).__name__, message=str(error),
                       timed_out=isinstance(error, subprocess.TimeoutExpired))
    finally:
        if child is not None:
            for kind in (signal.SIGTERM, signal.SIGKILL):
                if not exists(child.pid, group=True):
                    break
                try:
                    os.killpg(child.pid, kind)
                    receipt['cleanup_signals'].append(int(kind))
                except ProcessLookupError:
                    break
                try:
                    child.wait(timeout=1)
                except subprocess.TimeoutExpired:
                    pass
            receipt['exit_code'] = child.poll()
            receipt['owned_group_gone'] = not exists(child.pid, group=True)
        receipt.update(finished_utc=now(), elapsed_seconds=time.monotonic() - started)
        save(out / (name + '.exit.json'), receipt)  # Real exit is durable before parsing/asserting.
        save(out / (name + '.process.json'), {'pid': receipt['pid'], 'state': 'finished',
                                             'exit_code': receipt['exit_code']})
    if failure is not None:
        raise failure
    require(receipt.get('owned_group_gone') and receipt['exit_code'] is not None,
            'Owned group not confirmed stopped; do not start another child')
    return receipt


