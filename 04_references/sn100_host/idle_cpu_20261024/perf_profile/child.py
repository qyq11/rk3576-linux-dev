import json
import subprocess
import sys
import time
from pathlib import Path

out = Path(sys.argv[1])
process = subprocess.Popen(sys.argv[2:])
stat = (Path('/proc') / str(process.pid) / 'stat').read_text().rsplit(')', 1)[1].split()
(out / 'child_identity.json').write_text(json.dumps(dict(pid=process.pid, start_time_ticks=int(stat[19]),
    started_monotonic=time.monotonic(), command=sys.argv[2:]), indent=2) + '\n')
code = process.wait()
(out / 'child_exit.json').write_text(json.dumps(dict(exit_code=code, finished_monotonic=time.monotonic())) + '\n')
raise SystemExit(code if code >= 0 else 128 - code)
