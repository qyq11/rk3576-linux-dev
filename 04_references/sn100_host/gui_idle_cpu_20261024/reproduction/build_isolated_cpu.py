"""Controlled CPU experiment: old verified objects plus exactly one changed object."""
import hashlib
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess

root = Path('/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux')
old = root / 'temp/sn100_host/status_replay_20261024/native/wsl_release'
work = root / 'temp/sn100_host/o09_loopback/cpu_isolated'
work.mkdir()
text = (old / 'Makefile').read_text().replace('\\\n', ' ')
variables = dict(re.findall(r'^([A-Za-z_]+)\s*=\s*(.*)$', text, re.M))
def arguments(name):
    value = variables.get(name, '')
    for _ in range(8):
        expanded = re.sub(r'\$\((\w+)\)', lambda m: variables.get(m[1], ''), value)
        if expanded == value: break
        value = expanded
    assert '$(' not in value
    return shlex.split(value)
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
assert sha(old / 'sn100_shell') == '150c9f0335e564cc0cef367c82a876908afa1177657e3944864e01d12fd54bbc'
objects = arguments('OBJECTS')
assert len(objects) == 124 and len(set(objects)) == 124
material = root / '06_programs/sn100_host/shell/widgets/material'
shutil.copytree(material, work / 'material')
inputs_before = {str(p.relative_to(material)): sha(p) for p in material.rglob('*') if p.is_file()}
object_before = {}
for name in objects:
    path = work / name
    path.parent.mkdir(exist_ok=True)
    shutil.copy2(old / name, path)
    object_before[name] = sha(path)
changed = '.obj/qtmaterialflatbutton_internal.o'
compile_command = arguments('CXX') + ['-c'] + arguments('CXXFLAGS') + arguments('INCPATH') + [
    '-o', changed, str(work / 'material/qtmaterialflatbutton_internal.cpp')]
link_command = arguments('LINK') + arguments('LFLAGS') + ['-o', 'sn100_shell'] + objects + arguments('LIBS')
with (work / 'build.log').open('w') as log:
    subprocess.run(compile_command, cwd=work, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(link_command, cwd=work, stdout=log, stderr=subprocess.STDOUT, check=True)
differences = [name for name in objects if sha(work / name) != object_before[name]]
assert differences == [changed], differences
assert {str(p.relative_to(material)): sha(p) for p in material.rglob('*') if p.is_file()} == inputs_before
(work / 'verification.json').write_text(json.dumps({'baseline_binary_sha256': sha(old / 'sn100_shell'),
    'candidate_binary_sha256': sha(work / 'sn100_shell'), 'object_count': len(objects),
    'object_hashes_before': object_before, 'changed_objects': differences,
    'material_inputs': inputs_before, 'compile_command': compile_command, 'link_command': link_command,
    'scope': 'CPU experiment only; excludes concurrently changing instrument page. Final combined product needs its own build/regression'}, indent=2))
print('ISOLATED_CPU_CANDIDATE', sha(work / 'sn100_shell'))
