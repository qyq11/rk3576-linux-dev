"""Read-only original-log parser unit; never invokes a build or native helper."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time

base=Path(__file__).resolve().parent
root=base.parents[4]
work=base.parents[1]
out=base/sys.argv[1]
assert out.parent==base and not out.exists()
out.mkdir()
originals=[work/'platforms/windows_03',root/'temp/sn100_host/qc_local_20261025/platforms/windows']
def roster():
    return {str(p):{'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'bytes':p.stat().st_size}
            for directory in originals for p in sorted(directory.rglob('*')) if p.is_file()}
before=roster()
(out/'original_inputs_before.json').write_text(json.dumps(before,indent=2)+'\n')
command=[shutil.which('pwsh'),'-NoProfile','-File',str(base/'link_parser_unit.ps1'),'-Output',str(out)]
(out/'parser.command.json').write_text(json.dumps({'argv':command,'cwd':str(root),'timeout_seconds':30,'scope':'Read-only functions/parser; no Add-Type/qmake/make/g++/GUI'},indent=2)+'\n')
started=time.monotonic()
result=subprocess.run(command,cwd=root,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=30,check=False)
# Preserve original OS child exit before inspecting any parser output.
(out/'parser.exit.json').write_text(json.dumps({'actual_exit':result.returncode,'seconds':time.monotonic()-started,'timed_out':False},indent=2)+'\n')
(out/'parser.stdout.log').write_bytes(result.stdout)
(out/'parser.stderr.log').write_bytes(result.stderr)
after=roster()
(out/'original_inputs_after.json').write_text(json.dumps(after,indent=2)+'\n')
assert before==after,'Original build/log/Makefile/object/response files changed'
print(json.dumps({'actual_exit':result.returncode,'original_files_unchanged':len(before),'output':str(out)},indent=2))
raise SystemExit(result.returncode)
