"""Bind private diagnostics to one explicitly supplied completed native build."""
import hashlib
import json
from pathlib import Path
import re
import runpy
import shlex


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


class NativeInputs:
    def __init__(self, args, program, rehash):
        supplied = (args.snapshot, args.native_root, args.native_build, args.native_verification)
        require(all(supplied), 'Explicit --snapshot, --native-root, --native-build and --native-verification required')
        require(all(Path(value).is_absolute() for value in supplied), 'Native input paths must be absolute')
        self.snapshot_path, self.root, self.build, self.report_path = [
            Path(value).resolve(strict=True) for value in supplied]
        require(self.build.is_relative_to(self.root), 'Native build must belong to the supplied artifact root')
        self.snapshot_sha256 = sha(self.snapshot_path)
        snapshot = json.loads(self.snapshot_path.read_text(encoding='utf-8-sig'))
        report = json.loads(self.report_path.read_text(encoding='utf-8-sig'))
        require(isinstance(snapshot, dict) and snapshot, 'Nonempty exact source snapshot required')
        require(report.get('passed') is True and report.get('actual_exit') == 0,
                'Completed successful native verification required')
        require(report.get('snapshot_sha256') == self.snapshot_sha256
                and report.get('source_inputs') == len(snapshot), 'Native report and supplied snapshot differ')
        makefile_path = self.build / 'Makefile'
        require(sha(makefile_path) == report.get('makefile_sha256'), 'Native Makefile differs from report')
        # Planning reads receipts/paths only. Execution must rehash the whole
        # formal inventory, generated sources, all fresh objects and binaries.
        if rehash:
            for name, expected in snapshot.items():
                require(sha(program / name) == expected, 'Formal source changed: ' + name)
            inventory = runpy.run_path(str(program / 'shell/package_arm64.py'),
                                      run_name='sn100_bounded_probe_inventory')
            require(inventory['snapshot'](program) == snapshot, 'Exact formal source set changed')
        makefile = makefile_path.read_text().replace('\\\n', ' ')
        self.variables = dict(re.findall(r'^([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)$', makefile, re.M))
        self.variables.setdefault('SUBLIBS', '')
        all_objects = [(self.build / value).resolve(strict=True) for value in self.words('OBJECTS')]
        main = [path for path in all_objects if path.name == 'main.o']
        require(len(main) == 1, 'Exactly one native application main.o expected')
        objects = [path for path in all_objects if path not in main]
        require(objects and len(objects) == len(set(objects)), 'Unique nonempty production objects required')
        fresh = report.get('fresh_objects', {})
        require(fresh, 'Fresh native object receipts missing')
        receipt_objects = {}
        for relative, row in fresh.items():
            path = (self.root / relative).resolve(strict=True)
            require(path.is_relative_to(self.root) and row.get('created_after_start') is True,
                    'Fresh-object path/provenance invalid: ' + relative)
            if rehash:
                require(sha(path) == row['sha256'], 'Native object changed: ' + relative)
            receipt_objects[path] = row['sha256']
        require(all(path in receipt_objects for path in all_objects),
                'Selected Makefile objects absent from supplied native verification')
        rules = {str((self.build / obj).resolve()): (self.build / source).resolve(strict=True)
                 for obj, source in re.findall(r'^([^\s:]+\.o):\s+([^\s]+\.cpp)', makefile, re.M)}
        self.objects = {}
        for path in objects:
            require(str(path) in rules, 'No exact native source rule for ' + str(path))
            source = rules[str(path)]
            self.objects[str(path)] = {'object_sha256': receipt_objects[path], 'source': str(source),
                                      'source_sha256': sha(source)}
        require(all('QT_TESTLIB_LIB' not in value and 'QT_TESTCASE_BUILDDIR' not in value
                    for value in self.words('DEFINES')), 'Production objects must not be QtTest-instrumented')
        self.app = self.build / 'sn100_shell'
        self.artifacts = report.get('artifacts', {})
        for name in ('sn100_shell', 'sn100_report_renderer'):
            require(name in self.artifacts and (self.build / name).is_file(), 'Native artifact missing: ' + name)
        if rehash:
            for name, expected in self.artifacts.items():
                path = (self.build / name).resolve(strict=True)
                require(path.is_relative_to(self.root) and sha(path) == expected, 'Native artifact changed: ' + name)
        self.predefs = (self.build / '.moc/moc_predefs.h').resolve(strict=True)
        moc_paths = set(re.findall(r'^\t(\S*/moc)\s', makefile, re.M))
        require(len(moc_paths) == 1, 'One actual moc executable recipe expected')
        self.moc = str(Path(next(iter(moc_paths))).resolve(strict=True))
        core = Path(next(value for value in self.words('LIBS') if value.endswith('/libQt5Core.so')))
        self.test_library = core.with_name('libQt5Test.so').resolve(strict=True)
        self.qt_headers = Path(next(value[2:] for value in self.words('INCPATH')
                                    if value.endswith('/QtCore'))).parent
        self.metadata = {
            'snapshot': str(self.snapshot_path), 'snapshot_sha256': self.snapshot_sha256,
            'source_inputs': len(snapshot), 'native_root': str(self.root),
            'native_build': str(self.build), 'native_verification': str(self.report_path),
            'native_verification_sha256': sha(self.report_path), 'makefile_sha256': sha(makefile_path),
            'artifacts': self.artifacts, 'production_objects': self.objects,
            'production_object_count_excluding_main': len(objects),
            'fresh_object_count_all_reported_builds': len(fresh),
            'predefs_sha256': sha(self.predefs), 'moc': self.moc,
            'QtTest_library': str(self.test_library),
            'full_source_object_binary_rehash_performed': rehash,
        }

    def value(self, name, chain=()):
        require(name not in chain, 'Makefile variable cycle: ' + name)
        require(name in self.variables, 'Missing Makefile variable: ' + name)
        return re.sub(r'\$\(([^)]+)\)', lambda match: self.value(match[1], chain + (name,)),
                      self.variables[name])

    def words(self, name):
        return shlex.split(self.value(name))

    def commands(self, source, build, sanitizers):
        source = Path(source)
        build = Path(build)
        generated = build / '.moc' / (source.stem + '.moc')
        obj = build / '.obj' / (source.stem + '.o')
        target = build / source.stem
        definitions = ['-DQT_TESTLIB_LIB', '-DQT_TESTCASE_BUILDDIR="' + str(build) + '"']
        includes = ['-I' + str(build / '.moc'), '-I' + str(self.qt_headers / 'QtTest')]
        flags = ['-g', '-fno-omit-frame-pointer', '-fsanitize=address,undefined'] if sanitizers else []
        return [
            [self.moc] + self.words('DEFINES') + definitions + ['--include', str(self.predefs)]
                + includes + self.words('INCPATH') + [str(source), '-o', str(generated)],
            self.words('CXX') + self.words('CXXFLAGS') + definitions + includes
                + self.words('INCPATH') + ['-c', str(source), '-o', str(obj)] + flags,
            self.words('LINK') + self.words('LFLAGS') + ['-o', str(target), str(obj)]
                + list(self.objects) + self.words('LIBS') + [str(self.test_library)] + flags,
        ]

