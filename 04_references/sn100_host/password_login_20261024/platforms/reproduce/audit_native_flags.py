#!/usr/bin/env python3
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
new = ROOT / 'temp/sn100_host/o02_login/native/wsl_release'
old = ROOT / 'temp/sn100_host/o09_loopback/native/wsl_release'
fields = ('CXX', 'CXXFLAGS', 'DEFINES', 'INCPATH', 'LFLAGS', 'LIBS')


def flags(build):
    body = (build / 'Makefile').read_text().replace('\\\n', ' ')
    result = {}
    for name in fields:
        match = re.search(r'^' + name + r'\s*=\s*([^\n]*)', body, re.MULTILINE)
        assert match, name
        result[name] = match[1].strip()
    return result


a, b = flags(old), flags(new)
changes = [name for name in fields if a[name] != b[name]]
assert not changes, changes
report = {'old': a, 'new': b, 'all_selected_effective_fields_identical': True,
          'method': 'Exact Makefile field comparison, not timestamp inference',
          'compiler_command_sha256': hashlib.sha256(Path('/usr/bin/x86_64-linux-gnu-g++-13').read_bytes()).hexdigest()}
dest = ROOT / 'temp/sn100_host/o02_login/platforms/native_audit/flags.json'
dest.write_text(json.dumps(report, indent=2) + '\n')
print('NATIVE_FLAGS_MATCH_OLD_FROZEN_CACHE')
