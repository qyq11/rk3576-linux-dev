"""Explicit, read-only binding to a supplied fresh native build and receipt.

No default checkpoint paths and no mutable globals from the integration helpers.
The production packager remains the authority for the complete source inventory.
"""
import hashlib
import json
from pathlib import Path
import re
import runpy
import shlex


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''):
            digest.update(block)
    return digest.hexdigest()


def absolute_existing(path):
    value = Path(path)
    require(value.is_absolute(), 'Explicit absolute path required: ' + str(value))
    value = value.resolve(strict=True)
    require(not any(part.lower() in ('sn100_host_reference', 'seerain450_pc') for part in value.parts),
            'Reference project is not a native input')
    return value


def load_bound_json(path, expected_sha256):
    require(re.fullmatch('[0-9a-f]{64}', expected_sha256 or '') is not None,
            'Explicit lowercase SHA-256 required: ' + str(path))
    require(sha(path) == expected_sha256, 'Supplied input SHA differs: ' + str(path))
    value = json.loads(Path(path).read_text(encoding='utf-8-sig'))
    require(sha(path) == expected_sha256, 'Input changed while reading: ' + str(path))
    return value


def verify_full_source_inventory(program, frozen):
    require(isinstance(frozen, dict) and frozen, 'Nonempty complete source manifest required')
    inventory = runpy.run_path(str(program / 'shell/package_arm64.py'), run_name='report_probe_source_inventory')
    current = inventory['snapshot'](program)
    added = sorted(set(current) - set(frozen))
    removed = sorted(set(frozen) - set(current))
    require(not added and not removed,
            'Complete source set differs; added=' + repr(added) + '; removed=' + repr(removed))
    changed = sorted(name for name in frozen if current[name] != frozen[name])
    require(not changed, 'Source SHA differs: ' + repr(changed))
    return len(current)


def make_objects(build):
    body = (build / 'Makefile').read_text(encoding='utf-8')
    body = body.replace('\\\r\n', ' ').replace('\\\n', ' ')
    variables = dict(re.findall(r'^([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)$', body, re.M))

    def value(name, stack=()):
        require(name not in stack and name in variables, 'Invalid Makefile variable: ' + name)
        result = variables[name]
        for _ in range(32):
            if '$(' not in result:
                return result
            result = re.sub(r'\$\(([^)]+)\)', lambda match: value(match[1], stack + (name,)), result)
        raise RuntimeError('Unresolved Makefile variables: ' + name)

    tokens = shlex.split(value('OBJECTS'))
    paths = [(build / token).resolve(strict=True) for token in tokens]
    require(paths and len(set(paths)) == len(paths), 'Missing or duplicate Makefile objects')
    require(sum(path.name == 'main.o' for path in paths) == 1, 'Expected one actual main.o')
    return paths


def verify_native_binding(*, program, snapshot, snapshot_sha256, build,
                          verification, verification_sha256, renderer):
    program = absolute_existing(program)
    snapshot = absolute_existing(snapshot)
    build = absolute_existing(build)
    verification = absolute_existing(verification)
    renderer = absolute_existing(renderer)
    root = verification.parent
    require(build.is_dir() and build.is_relative_to(root),
            'Explicit build directory must belong to the supplied receipt root')
    require(renderer.is_file() and renderer.is_relative_to(root),
            'Renderer must belong to the supplied receipt root')
    frozen = load_bound_json(snapshot, snapshot_sha256)
    report = load_bound_json(verification, verification_sha256)
    count = verify_full_source_inventory(program, frozen)
    require(report.get('passed') is True and report.get('actual_exit') == 0,
            'Fresh native receipt must record passed=true and actual_exit=0')
    require(report.get('source_inputs') == count and report.get('snapshot_sha256') == snapshot_sha256,
            'Native receipt is not bound to the supplied complete snapshot')
    makefile = build / 'Makefile'
    makefile_sha = sha(makefile)
    require(report.get('makefile_sha256') == makefile_sha,
            'Explicit build Makefile does not match supplied receipt')
    selected = {'sn100_shell': build / 'sn100_shell', 'sn100_report_renderer': renderer}
    artifacts = {}
    for name, path in selected.items():
        actual = sha(path)
        require(report.get('artifacts', {}).get(name) == actual,
                'Artifact does not match supplied native receipt: ' + name)
        artifacts[name] = {'path': str(path), 'sha256': actual}
    fresh = report.get('fresh_objects')
    require(isinstance(fresh, dict) and fresh, 'Missing full fresh-object receipt')
    checked = {}
    for relative, row in fresh.items():
        require(isinstance(relative, str) and not Path(relative).is_absolute(), 'Object receipt key must be relative')
        path = (root / relative).resolve(strict=True)
        require(path.is_relative_to(root) and path.suffix == '.o', 'Object escapes supplied receipt root')
        require(isinstance(row, dict) and row.get('created_after_start') is True,
                'Object has no fresh-build evidence: ' + relative)
        actual = sha(path)
        require(row.get('sha256') == actual, 'Fresh object SHA differs: ' + relative)
        require(str(path) not in checked, 'Duplicate object alias in receipt')
        checked[str(path)] = actual
    actual_objects = make_objects(build)
    for path in actual_objects:
        require(str(path) in checked, 'Actual link object absent from fresh receipt: ' + str(path))
    verify_full_source_inventory(program, frozen)
    require(sha(snapshot) == snapshot_sha256 and sha(verification) == verification_sha256,
            'Snapshot or native receipt changed during verification')
    return {'schema': 'sn100-report-probe-explicit-native-binding-v2',
            'program': str(program), 'snapshot': str(snapshot), 'snapshot_sha256': snapshot_sha256,
            'verification': str(verification), 'verification_sha256': verification_sha256,
            'receipt_root': str(root), 'build': str(build), 'source_inputs': count,
            'makefile_sha256': makefile_sha, 'artifacts': artifacts,
            'actual_shell_object_count': len(actual_objects),
            'actual_shell_objects_excluding_main': len(actual_objects) - 1,
            'all_receipted_object_sha256': checked}
