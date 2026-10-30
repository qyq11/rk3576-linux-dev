import collections
import hashlib
import json
import os
import re
import shutil
import stat
import struct
from pathlib import Path

ROOT = Path.cwd().resolve()
TEMP = ROOT / 'temp'
OUT = ROOT / '04_references/sn100_host/cleanup_20261030'
OUT.mkdir(parents=True, exist_ok=True)

copies = {
    'temp/sn100_host/original_db_20261027/integration/pause_handoff_20261028.md': 'handoff/pause_handoff_20261028.md',
    'temp/sn100_host/original_db_20261027/integration/stop_handoff_20261027.md': 'handoff/stop_handoff_20261027.md',
    'temp/sn100_host/original_db_20261027/integration/pause_working_identity_20261028.json': 'handoff/pause_working_identity_20261028.json',
    'temp/sn100_host/button_controls_20261027/new_chat_handoff.md': 'handoff/sample_reagent_handoff_20261028.md',
    'temp/sn100_host/button_controls_20261027/reagent_batch_source_sha256.json': 'handoff/reagent_batch_source_sha256.json',
}
candidate = TEMP / 'sn100_host/button_controls_20261027/reagent_date_candidate'
for path in candidate.iterdir():
    assert path.is_file()
    copies[path.relative_to(ROOT).as_posix()] = 'unapplied_date_candidate/' + path.name
archived = []
for source, target in copies.items():
    src, dst = ROOT / source, OUT / target
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(src, dst)
    digest = hashlib.sha256(src.read_bytes()).hexdigest()
    assert digest == hashlib.sha256(dst.read_bytes()).hexdigest()
    archived.append({'source': source, 'archive': dst.relative_to(ROOT).as_posix(), 'sha256': digest})

def executable_elf(path):
    try:
        with path.open('rb') as stream:
            header = stream.read(64)
            if header[:4] != b'\x7fELF' or header[5] != 1:
                return False
            kind = struct.unpack_from('<H', header, 16)[0]
            if kind == 2:
                return True
            if kind != 3:
                return False
            if header[4] == 2:
                offset = struct.unpack_from('<Q', header, 32)[0]
                size, count = struct.unpack_from('<HH', header, 54)
            elif header[4] == 1:
                offset = struct.unpack_from('<I', header, 28)[0]
                size, count = struct.unpack_from('<HH', header, 42)
            else:
                return False
            if count > 256 or size < 4:
                return False
            for number in range(count):
                stream.seek(offset + size * number)
                if stream.read(4) == b'\x03\x00\x00\x00':
                    return True
    except (OSError, IndexError, struct.error):
        pass
    return False

files = []
protected = collections.Counter()
skipped = []
for folder, directories, names in os.walk(TEMP, followlinks=False):
    for name in list(directories):
        path = Path(folder) / name
        info = path.stat(follow_symlinks=False)
        if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 1024:
            directories.remove(name)
    for name in names:
        path = Path(folder) / name
        rel = path.relative_to(ROOT).as_posix()
        info = path.stat(follow_symlinks=False)
        if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 1024:
            continue
        parts = path.relative_to(TEMP).parts
        if any('sysroot' in part.lower() or part.lower() in {'loader', 'loaders', 'runtime'} for part in parts[:-1]):
            protected['dependency_or_runtime'] += 1
            continue
        reason = None
        if path.suffix.lower() in {'.o', '.obj', '.pyc', '.pyo', '.pch', '.ilk', '.pdb'}:
            reason = 'compiler_or_python_cache'
        elif re.match(r'^(moc_.*\.(cpp|h)|qrc_.*\.cpp|ui_.*\.h)$', name) or name.endswith('.moc'):
            reason = 'qt_generated_source'
        elif name in {'Makefile', 'Makefile.Debug', 'Makefile.Release', '.qmake.stash', '.qmake.cache'}:
            reason = 'generated_build_rules'
        elif path.suffix.lower() == '.exe' and parts[0] == 'sn100_host' and not re.search(r'install|setup|redistribut', name, re.I):
            with path.open('rb') as stream:
                if stream.read(2) == b'MZ':
                    reason = 'compiled_windows_program'
        elif re.fullmatch(r'sn100_shell_[0-9a-f]{64}\.tar\.gz', name) and 'packages' in str(path.parent).lower():
            reason = 'generated_application_package'
        elif path.suffix.lower() in {'', '.new'} and info.st_size > 64 and executable_elf(path):
            reason = 'compiled_linux_program'
        if reason:
            files.append({'path': rel, 'bytes': info.st_size, 'mtime_ns': info.st_mtime_ns, 'reason': reason})

totals = collections.defaultdict(lambda: {'files': 0, 'bytes': 0})
for item in files:
    totals[item['reason']]['files'] += 1
    totals[item['reason']]['bytes'] += item['bytes']
result = {'date': '2026-10-30', 'root': str(ROOT), 'scope': 'temp only; generated outputs; no recursive directory deletion',
          'archived_handoffs_and_candidate': archived, 'summary': dict(totals), 'protected': dict(protected), 'files': files}
(OUT / 'cleanup_manifest.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'archived': len(archived), 'summary': dict(totals), 'protected': dict(protected),
                  'total_files': len(files), 'total_bytes': sum(x['bytes'] for x in files)}, ensure_ascii=False, indent=2))
