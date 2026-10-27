#!/usr/bin/env python3
"""Per-invocation ARM compiler receipts, scoped to this isolated build only."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

base = Path(__file__).resolve().parent
directory = base / 'compiler_receipts'
directory.mkdir(exist_ok=True)
args = sys.argv[1:]
compiler = '/usr/bin/aarch64-linux-gnu-g++'
started_ns = time.time_ns()
receipt = directory / ('invocation_' + str(started_ns) + '_' + str(os.getpid()) + '.json')
record = {'compiler': compiler, 'argv': [compiler, *args], 'cwd': str(Path.cwd()),
          'started_ns': started_ns,
          'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
          'scope': 'Exact argv delivered to real compiler, no shell/xtrace parsing or flag changes'}
if '-c' in args:
    record['source'] = args[args.index('-c') + 1]
if '-o' in args:
    record['output'] = args[args.index('-o') + 1]
receipt.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
result = subprocess.run(record['argv'], check=False)
record.update(exit_code=result.returncode, finished_ns=time.time_ns(),
              finished_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
output = Path(record['output']) if 'output' in record else None
if output and output.is_file():
    with output.open('rb') as stream:
        digest = hashlib.sha256()
        for block in iter(lambda: stream.read(1048576), b''):
            digest.update(block)
    record['output_sha256'] = digest.hexdigest()
    record['output_bytes'] = output.stat().st_size
temporary = receipt.with_suffix('.writing')
temporary.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
temporary.replace(receipt)
raise SystemExit(result.returncode if result.returncode >= 0 else 128 - result.returncode)
