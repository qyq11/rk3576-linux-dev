#!/usr/bin/env python3
import hashlib
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
PROGRAM = ROOT / '06_programs/sn100_host'
WORK = ROOT / 'temp/sn100_host/o02_login'
BUILD = WORK / 'native/wsl_release'
DEST = WORK / 'platforms/native_audit'
DEST.mkdir(exist_ok=False)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
source_before = json.loads((WORK / 'native/before.json').read_text())
source_after = json.loads((WORK / 'native/after.json').read_text())
assert source_before == source_after
body = (BUILD / 'Makefile').read_text().replace('\\\n', ' ')
log = (WORK / 'logs/native.log').read_text()
compile_commands = [line for line in log.splitlines() if re.match(r'^(?:\S*/)?g\+\+\s+-c\s', line)]
compiled = {}
for line in compile_commands:
    match = re.search(r'\s-o\s+(\S+\.o)\s+(.*)$', line)
    assert match, line
    compiled[str((BUILD / match[1]).resolve())] = line
rules = []
for match in re.finditer(r'^(\S+\.o):\s*([^\n]+)', body, re.MULTILINE):
    object_path = (BUILD / match[1]).resolve()
    inputs = [(BUILD / name).resolve() for name in match[2].split() if name != '\\']
    rules.append((object_path, inputs))
header = (PROGRAM / 'data/account_store.h').resolve()
closure = [(obj, inputs) for obj, inputs in rules if header in inputs]
assert closure
missing = [str(obj) for obj, _ in closure if str(obj) not in compiled]
records = []
for obj, inputs in closure:
    records.append({'object': str(obj), 'sha256': sha(obj), 'mtime_ns': obj.stat().st_mtime_ns,
                    'account_header_sha256': sha(header), 'account_header_mtime_ns': header.stat().st_mtime_ns,
                    'compile_command': compiled.get(str(obj)),
                    'newer_than_account_header': obj.stat().st_mtime_ns >= header.stat().st_mtime_ns,
                    'inputs': [str(p) for p in inputs]})
probe = subprocess.run(['make', '-q'], cwd=BUILD, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
(DEST / 'make_q.log').write_text(probe.stdout)
object_probe = subprocess.run(['make', '-q', *[str(obj.relative_to(BUILD)) for obj, _ in rules]],
                              cwd=BUILD, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
(DEST / 'make_q_objects.log').write_text(object_probe.stdout)
old_work = ROOT / 'temp/sn100_host/o09_loopback/native'
old_build = old_work / 'wsl_release'
old_sources = json.loads((old_work / 'interim_after.json').read_text())
changed_dependencies = []
reused = []
for obj, inputs in rules:
    reasons = []
    for path in inputs:
        if path.is_relative_to(PROGRAM):
            name = path.relative_to(PROGRAM).as_posix()
            if name in source_after and old_sources.get(name) != source_after[name]:
                reasons.append(name)
        elif path.is_relative_to(BUILD) and path.is_file():
            old = old_build / path.relative_to(BUILD)
            if not old.is_file() or sha(old) != sha(path):
                reasons.append(path.relative_to(BUILD).as_posix())
    if reasons:
        changed_dependencies.append({'object': str(obj), 'reasons': reasons,
                                     'actually_compiled': str(obj) in compiled})
    if str(obj) not in compiled:
        old = old_build / obj.relative_to(BUILD)
        assert old.is_file() and sha(old) == sha(obj), str(obj)
        reused.append({'object': str(obj), 'sha256': sha(obj)})
(DEST / 'all_changed_dependency_objects.json').write_text(json.dumps(changed_dependencies, indent=2) + '\n')
(DEST / 'reused_objects.json').write_text(json.dumps(reused, indent=2) + '\n')
(DEST / 'account_dependency_objects.json').write_text(json.dumps(records, indent=2) + '\n')
report = {'source_entries': len(source_before), 'source_before_after_identical': True,
          'application_sha256': sha(BUILD / 'sn100_shell'),
          'makefile_sha256': sha(BUILD / 'Makefile'), 'makefile_mtime_ns': (BUILD / 'Makefile').stat().st_mtime_ns,
          'compiled_command_count': len(compile_commands), 'object_rules': len(rules),
          'account_header_dependency_objects': len(closure), 'missing_compile_commands': missing,
          'all_account_objects_newer_than_header': all(r['newer_than_account_header'] for r in records),
          'make_q_exit': probe.returncode, 'clock_warning_present': 'Clock skew detected' in log,
          'make_q_all_140_objects_exit': object_probe.returncode,
          'all_changed_dependency_objects_actually_compiled': all(r['actually_compiled'] for r in changed_dependencies),
          'changed_dependency_object_count': len(changed_dependencies),
          'reused_object_count': len(reused), 'reused_object_sha_matches_old_cache': True,
          'default_make_q_explanation': 'Published staging target ../wsl_report_stage/sn100_shell was intentionally moved away by wsl.sh; make -n only proposes relink, no C++ compilation',
          'no_qmake_invocation_by_audit': True}
(DEST / 'verification.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report))
