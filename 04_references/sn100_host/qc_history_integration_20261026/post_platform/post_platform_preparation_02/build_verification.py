"""Fresh build inventory plus positive per-target business-source verification."""
import os
from pathlib import Path
import re
import shlex
from common import PROGRAM, require, sha
from build_roles import parse_graph, verify_role

def file_sha(path):
    path = Path(path)
    return sha(path) if path.is_file() and not path.is_symlink() else None

def make_words(makefile, variable):
    text = makefile.read_text(encoding='utf-8').replace('\\\n', ' ')
    rows = re.findall(r'^' + re.escape(variable) + r'\s*=\s*(.*)$', text, re.MULTILINE)
    require(len(rows) == 1, 'Expected one Makefile variable: ' + variable)
    return shlex.split(rows[0])

def build_inventory(build, binary):
    objects = {p.relative_to(build).as_posix(): sha(p)
               for p in sorted(build.rglob('*.o')) if p.is_file()}
    generated = {p.relative_to(build).as_posix(): sha(p) for p in sorted(build.rglob('*'))
                 if p.is_file() and (p.parent.name in ('.moc', '.ui', '.rcc')
                                     or p.name in ('Makefile', '.qmake.stash'))}
    return {'binary_sha256': file_sha(binary), 'objects_sha256': objects,
            'generated_sha256': generated}

def verify_build(build, binary, frozen, started_ns):
    makefile = build / 'Makefile'
    text = makefile.read_text(encoding='utf-8')
    require('-O2' in text and '-g ' not in text, 'Fresh Makefile must use Release flags')
    require(make_words(makefile, 'TARGET') == [binary.name], 'Unexpected qmake TARGET')
    objects = make_words(makefile, 'OBJECTS')
    require(objects and len(set(objects)) == len(objects), 'Missing/duplicate Makefile OBJECTS')
    expected_objects = {Path(word).as_posix() for word in objects}
    inventory = build_inventory(build, binary)
    require(set(inventory['objects_sha256']) == expected_objects,
            'Actual fresh object set differs from Makefile OBJECTS')
    for word in objects:
        path = (build / word).resolve(strict=True)
        require(path.is_relative_to(build.resolve()), 'Object escapes private build: ' + word)
        require(path.stat().st_mtime_ns + 2_000_000_000 >= started_ns,
                'Object timestamp predates its fresh build: ' + word)
    require(binary.is_file() and os.access(binary, os.X_OK), 'Fresh executable missing')
    require(binary.stat().st_mtime_ns + 2_000_000_000 >= started_ns, 'Executable predates build')
    source_rows = []
    for word in make_words(makefile, 'SOURCES'):
        path = (build / word).resolve(strict=True)
        if path.is_relative_to(PROGRAM.resolve()):
            name = path.relative_to(PROGRAM.resolve()).as_posix()
            require(name in frozen and sha(path) == frozen[name], 'Unfrozen qmake source: ' + name)
            source_rows.append({'source_relative': name, 'sha256': sha(path), 'generated': False})
        else:
            require(path.is_relative_to(build.resolve()), 'Unexpected external qmake source: ' + str(path))
            source_rows.append({'source_relative': path.relative_to(build).as_posix(),
                                'sha256': sha(path), 'generated': True})
    graph = parse_graph(text, PROGRAM.resolve().as_posix(), build.resolve().as_posix())
    role_proof = verify_role(graph)
    require(set(graph['declared_sources']) == {row['source_relative'] for row in source_rows},
            'Role graph differs from actually fingerprinted qmake sources')
    return inventory, {'sources': source_rows, 'objects': objects, 'target_role': role_proof,
                       'fresh_empty_build_directory': True, 'object_timestamp_tolerance_seconds': 2,
                       'makefile_sha256': sha(makefile), 'binary_sha256': sha(binary)}
