"""Fresh narrow ARM64 compile with existing read-only sysroot; no deployment."""
from pathlib import Path
import datetime
import hashlib
import json
import os
import signal
import subprocess

if not __debug__ or os.geteuid() == 0:
    raise RuntimeError('Ordinary WSL user and assertions required')
work = Path(__file__).resolve().parent
root = work.parents[2]
program = root / '06_programs/sn100_host'
out = work / 'protocol_log_scope/arm64'
out.mkdir(exist_ok=False)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
read = lambda p: json.loads(p.read_text(encoding='utf-8-sig'))
native = read(work / 'protocol_log_scope/verification.json')
assert native['passed']
before = native['source_inputs']
assert all(sha(program / name) == digest for name, digest in before.items())
sysroot = root / 'temp/sn100_host/o02_login/platforms/arm64/sysroot'
include = sysroot / 'usr/include/aarch64-linux-gnu/qt5'
lib = sysroot / 'usr/lib/aarch64-linux-gnu'
libs = [lib / ('libQt5' + name + '.so.5.15.13') for name in ('Core', 'Network')]
assert include.is_dir() and all(p.is_file() and p.resolve().is_relative_to(sysroot) for p in libs)
tools = [Path('/usr/bin/aarch64-linux-gnu-g++'), Path('/usr/lib/qt5/bin/moc'), Path('/usr/bin/aarch64-linux-gnu-readelf')]
fingerprints = {str(p): sha(p.resolve()) for p in libs + tools}
def save(name, value):
    (out / name).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n')
steps = []
def run(name, argv):
    row = {'name': name, 'argv': list(map(str, argv)), 'cwd': str(out), 'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat()}
    save(name + '.command.json', row)
    with (out / (name + '.log')).open('xb') as stream:
        p = subprocess.Popen(row['argv'], cwd=out, stdout=stream, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            code = p.wait(timeout=90)
        except BaseException:
            try:
                os.killpg(p.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                p.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(p.pid, signal.SIGKILL)
                p.wait()
            raise
        finally:
            row['exit_code'] = p.returncode
            steps.append(row)
            save(name + '.exit.json', row)
            save('steps.json', steps)
    assert code == 0, row
    print(name, 'actual_exit', code, flush=True)
try:
    save('source_before.json', before)
    save('external_before.json', fingerprints)
    run('compiler_version', [tools[0], '-dumpfullversion'])
    for stem in ('read_only_client', 'tcp_transport'):
        run('moc_' + stem, [tools[1], program / 'protocol' / (stem + '.h'), '-o', out / ('moc_' + stem + '.cpp')])
    sources = [program / 'protocol' / (name + '.cpp') for name in
        ('soak_main', 'read_only_client', 'tcp_transport', 'read_only_messages', 'frame_codec', 'status_session')]
    sources += [out / ('moc_' + name + '.cpp') for name in ('read_only_client', 'tcp_transport')]
    flags = ['-std=c++14', '-O2', '-g', '-DQT_NO_DEBUG', '-fPIC', '-Wall', '-Wextra',
        '-I' + str(program / 'protocol')]
    for path in (include, include / 'QtCore', include / 'QtNetwork'):
        flags += ['-isystem', str(path)]
    objects = []
    for i, source in enumerate(sources):
        target = out / ('obj_' + str(i) + '.o')
        run('compile_' + str(i), [tools[0], *flags, '-c', source, '-o', target])
        objects.append(target)
    binary = out / 'sn100_protocol_soak'
    run('link', [tools[0], *objects, '-L' + str(lib), '-L' + str(sysroot / 'lib/aarch64-linux-gnu'),
        '-Wl,-rpath-link,' + str(lib), '-Wl,-rpath-link,' + str(sysroot / 'lib/aarch64-linux-gnu'),
        '-l:libQt5Network.so.5.15.13', '-l:libQt5Core.so.5.15.13', '-lpthread', '-o', binary])
    run('elf_header', [tools[2], '-h', binary])
    run('elf_needed', [tools[2], '-d', binary])
    data = binary.read_bytes()
    assert data[:6] == b'\x7fELF\x02\x01' and int.from_bytes(data[18:20], 'little') == 183
    assert all(sha(program / name) == digest for name, digest in before.items())
    assert fingerprints == {str(p): sha(p.resolve()) for p in libs + tools}
    save('verification.json', {'passed': True, 'source_inputs': before, 'source_unchanged': True,
        'sysroot': str(sysroot), 'external_inputs_unchanged': fingerprints, 'binary_sha256': sha(binary),
        'fresh_objects': {p.name: sha(p) for p in objects}, 'objects_reused': 0,
        'all_child_exit_codes': [row['exit_code'] for row in steps], 'board_executed': False,
        'scope': 'Narrow protocol-soak fresh ARM64 compile/ELF only; no board run or whole-app release claim'})
except BaseException as error:
    save('failure.json', {'passed': False, 'error': str(error), 'steps': steps})
    raise
