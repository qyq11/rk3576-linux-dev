#!/usr/bin/env python3
"""Link QtTest drivers against verified Release main objects, excluding main()."""
import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess
from native_binding import verify_native_binding, verify_full_source_inventory


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--snapshot', type=Path, required=True)
    parser.add_argument('--snapshot-sha256', required=True)
    parser.add_argument('--native-verification', type=Path, required=True)
    parser.add_argument('--native-verification-sha256', required=True)
    parser.add_argument('--renderer', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--jobs', type=int, choices=(1, 2), default=2)
    parser.add_argument('--reuse-dir', type=Path)
    parser.add_argument('drivers', nargs='+')
    args = parser.parse_args()
    program = Path('/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host')
    native = args.build_dir.resolve(strict=True)
    build = args.output_dir
    if not build.is_absolute() or not args.snapshot.is_absolute():
        parser.error('Output and frozen snapshot paths must be absolute')
    if any(not re.fullmatch(r'[a-z][a-z0-9_]*', name) for name in args.drivers):
        parser.error('Drivers must be test source stems')
    if args.drivers != ['integrated_report_test'] or args.reuse_dir:
        parser.error('This private driver only builds the fresh diagnostic test copy')
    binding_args = dict(program=program, snapshot=args.snapshot, snapshot_sha256=args.snapshot_sha256,
        build=args.build_dir, verification=args.native_verification,
        verification_sha256=args.native_verification_sha256, renderer=args.renderer)
    binding_before = verify_native_binding(**binding_args)
    prepared = json.loads((Path(__file__).parent / 'prepared_sources.json').read_text())
    before = json.loads(args.snapshot.read_text())

    def check_sources():
        verify_full_source_inventory(program, before)

    check_sources()
    build.mkdir(parents=True, exist_ok=False)
    for directory in ('.obj', '.moc', 'logs'):
        (build / directory).mkdir(exist_ok=True)
    makefile = (native / 'Makefile').read_text().replace('\\\n', ' ')
    variables = dict(re.findall(r'^([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)$', makefile, re.M))
    variables.setdefault('SUBLIBS', '')

    def value(name):
        text = variables[name]
        while '$(' in text:
            text = re.sub(r'\$\(([^)]+)\)', lambda match: value(match[1]), text)
        return text

    def expand(name):
        return shlex.split(value(name))

    objects = [(native / name).resolve(strict=True) for name in expand('OBJECTS')]
    entry = [path for path in objects if path.name == 'main.o']
    if len(entry) != 1:
        raise RuntimeError('Expected exactly one main.o')
    objects = [path for path in objects if path not in entry]
    rules = {str((native / obj).resolve()): (native / source).resolve(strict=True)
             for obj, source in re.findall(r'^([^\s:]+\.o):\s+([^\s]+\.cpp)', makefile, re.M)}
    common = {str(path): {'object_sha256': sha(path), 'source': str(rules[str(path)]),
                         'source_sha256': sha(rules[str(path)])} for path in objects}
    reuse = None
    if args.reuse_dir:
        args.reuse_dir = args.reuse_dir.resolve(strict=True)
        if args.reuse_dir == build.resolve():
            raise RuntimeError('Reuse origin must be a preserved, separate driver directory')
        reuse = json.loads((args.reuse_dir / 'binding_proof.json').read_text())
        if (reuse['native_makefile_sha256'] != sha(native / 'Makefile')
                or reuse['snapshot_sha256'] != sha(args.snapshot)
                or reuse['native_objects'] != common):
            raise RuntimeError('Test reuse requires identical frozen production sources, flags and objects')
    defines, flags, includes = (expand(name) for name in ('DEFINES', 'CXXFLAGS', 'INCPATH'))
    if any('QT_TESTLIB_LIB' in item or 'QT_TESTCASE_BUILDDIR' in item for item in defines):
        raise RuntimeError('Main Release objects must not be test-instrumented')
    qt_headers = Path(next(item[2:] for item in includes if item.endswith('/QtCore'))).parent
    core = Path(next(item for item in expand('LIBS') if item.endswith('/libQt5Core.so')))
    test_library = core.with_name('libQt5Test.so').resolve(strict=True)
    test_defines = ['-DQT_TESTLIB_LIB', '-DQT_TESTCASE_BUILDDIR="' + str(build) + '"']
    test_includes = ['-I' + str(build / '.moc'), '-I' + str(qt_headers / 'QtTest')]
    predefs = (native / '.moc/moc_predefs.h').resolve(strict=True)

    def build_driver(name):
        # Keep the existing independent calibration tests at their authoritative
        # paths while linking them against the same Release business objects.
        independent = {
            'calibration_store_test': 'calibration/tests/calibration_store_test.cpp',
            'calibration_page_test': 'calibration/tests/calibration_page_test.cpp',
        }
        source = Path('/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/offline_followup_20261026/report_query/source/integrated_report_test.cpp').resolve(strict=True)
        if (sha(source) != prepared['trace_sha256']
                or sha(source.parent.parent / 'report_query_trace.h') != prepared['trace_header_sha256']):
            raise RuntimeError('Prepared diagnostic copy or header changed')
        generated, obj, target = build / '.moc' / (name + '.moc'), build / '.obj' / (name + '.o'), build / name
        previous = reuse['drivers'].get(name) if reuse else None
        if previous and previous['source_sha256'] == sha(source):
            if (previous['source_relative'] != source.as_posix()
                    or not all(row['exit_code'] == 0 for row in previous['receipts'])):
                raise RuntimeError('Invalid preserved driver provenance: ' + name)
            copied = []
            for relative, expected in ((name,previous['binary_sha256']),
                    ('.moc/' + name + '.moc',previous['moc_sha256']),
                    ('.obj/' + name + '.o',previous['object_sha256'])):
                old, new = args.reuse_dir / relative, build / relative
                if sha(old) != expected:
                    raise RuntimeError('Preserved test artifact changed: ' + str(old))
                shutil.copy2(old,new)
                if sha(new) != expected:
                    raise RuntimeError('Copied test artifact differs: ' + str(new))
                copied.append({'original':str(old),'current':str(new),'sha256':expected})
            shutil.copy2(args.reuse_dir / 'logs' / (name+'.log'),build / 'logs' / (name+'.log'))
            receipt = {'reused_from':str(args.reuse_dir),'old_proof_sha256':sha(args.reuse_dir/'binding_proof.json'),
                'reason':'Unchanged test source and exact same current frozen production objects/Makefile; compiler not rerun',
                'artifacts':copied}
            (build/(name+'.reuse.json')).write_text(json.dumps(receipt,indent=2)+'\n')
            return name, dict(previous,reused_from=str(args.reuse_dir),reuse_receipt=name+'.reuse.json')
        commands = [
            ['/usr/lib/qt5/bin/moc'] + defines + test_defines + ['--include', str(predefs)] +
            test_includes + includes + [str(source), '-o', str(generated)],
            expand('CXX') + flags + test_defines + test_includes + includes + ['-c', str(source), '-o', str(obj)],
            expand('LINK') + expand('LFLAGS') + ['-o', str(target), str(obj)] +
            [str(path) for path in objects] + expand('LIBS') + [str(test_library)]]
        receipts = []
        with (build / 'logs' / (name + '.log')).open('w') as log:
            for command in commands:
                log.write(shlex.join(command) + '\n')
                log.flush()
                result = subprocess.run(command, cwd=native, stdout=log, stderr=subprocess.STDOUT)
                receipts.append({'command': command, 'exit_code': result.returncode})
                if result.returncode:
                    (build / (name + '.failed.json')).write_text(json.dumps(receipts, indent=2) + '\n')
                    raise RuntimeError(name + ' failed; see ' + str(log.name))
        return name, {'source_relative': source.as_posix(),
                      'source_sha256': sha(source), 'binary_sha256': sha(target),
                      'moc_sha256': sha(generated), 'object_sha256': sha(obj), 'receipts': receipts}

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        drivers = dict(pool.map(build_driver, args.drivers))
    check_sources()
    for path in objects:
        if sha(path) != common[str(path)]['object_sha256'] or sha(rules[str(path)]) != common[str(path)]['source_sha256']:
            raise RuntimeError('Main objects changed during test driver build: ' + str(path))
    binding_after = verify_native_binding(**binding_args)
    if binding_after != binding_before:
        raise RuntimeError('Supplied native binding changed during driver compilation')
    proof = {'supplied_native_binding': binding_before,
             'native_makefile_sha256': sha(native / 'Makefile'), 'snapshot_sha256': sha(args.snapshot),
             'native_objects_excluding_main': len(objects), 'native_objects': common, 'drivers': drivers}
    (build / 'binding_proof.json').write_text(json.dumps(proof, indent=2) + '\n')
    print(json.dumps({'drivers': list(drivers), 'objects': len(objects),
                     'reused_test_drivers':sum('reused_from' in row for row in drivers.values()),'passed': True}), flush=True)


if __name__ == '__main__':
    main()
