"""Explicitly authorized short helper probes only; no controller or Qt build."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time

base=Path(__file__).resolve().parent
attempt=sys.argv[1]
assert attempt.startswith('run_') and '/' not in attempt and '\\' not in attempt
out=base/attempt
assert not out.exists()
out.mkdir()
root=base.parents[4]
failed=base.parents[1]/'platforms/windows_02'
if failed.is_dir():
    before={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in failed.iterdir() if p.is_file()}
    (out/'windows_02_members_before.json').write_text(json.dumps(before,indent=2)+'\n')
    original=failed/'source_before.json.snapshot.log.stderr.log'
    shutil.copyfile(original,out/'windows_02_original_stderr.raw')
results=[]
for mode in ('windows','wsl'):
    directory=out/mode
    directory.mkdir()
    command=[shutil.which('pwsh'),'-NoProfile','-File',str(base/'windows_job_probe.ps1'),'-Mode',mode,'-Output',str(directory)]
    (directory/'parent.command.json').write_text(json.dumps({'argv':command,'cwd':str(root),'timeout_seconds':30},indent=2)+'\n')
    started=time.monotonic()
    try:
        result=subprocess.run(command,cwd=root,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=30,check=False)
        receipt={'actual_exit':result.returncode,'timed_out':False,'seconds':time.monotonic()-started}
        # Save the actual parent exit before reading any child output.
        (directory/'parent.exit.json').write_text(json.dumps(receipt,indent=2)+'\n')
        (directory/'parent.stdout.log').write_bytes(result.stdout)
        (directory/'parent.stderr.log').write_bytes(result.stderr)
    except subprocess.TimeoutExpired as error:
        receipt={'actual_exit':None,'timed_out':True,'seconds':time.monotonic()-started,'error':str(error)}
        (directory/'parent.exit.json').write_text(json.dumps(receipt,indent=2)+'\n')
    results.append({'mode':mode,**receipt})
if failed.is_dir():
    after={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in failed.iterdir() if p.is_file()}
    assert before==after
    (out/'windows_02_members_after.json').write_text(json.dumps(after,indent=2)+'\n')
(out/'summary.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps(results,indent=2))
raise SystemExit(0 if all(row['actual_exit']==0 and not row['timed_out'] for row in results) else 1)
