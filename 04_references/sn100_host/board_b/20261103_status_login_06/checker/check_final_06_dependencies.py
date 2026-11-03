#!/usr/bin/env python3
"""检查新06检查器的ARM64依赖文件，不执行ARM程序，也不连接板卡。"""
import hashlib
import json
import subprocess
import sys
from pathlib import Path

here = Path(__file__).resolve().parent
build_result = json.loads((here / 'execution_06_result.json').read_text())
if not build_result['checker_built']:
    raise RuntimeError('尚无成功的新06检查器构建记录')
output = here / 'checker_dependencies'
if output.exists() or (here / 'checker_dependency_check.log').exists():
    raise RuntimeError('本批依赖检查证据已存在，不覆盖')
binary = here / 'status_login_review_06'
def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()
if sha(binary) != build_result['checker_sha256']:
    raise RuntimeError('检查器在构建后已发生变化')
provenance = json.loads((here / 'review_build_provenance.json').read_text())
command = [sys.executable, str(here / 'frozen_06/sn100_host/shell/check_arm64.py'),
           str(binary), '--sysroot', provenance['sysroot'], '--output-dir', str(output)]
with (here / 'checker_dependency_check.log').open('xb') as stream:
    result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT)
with (here / 'checker_dependency_check.exit').open('x') as stream:
    stream.write(str(result.returncode) + '\n')
unchanged = sha(binary) == build_result['checker_sha256']
record = {'command': command, 'actual_exit_code': result.returncode,
          'checker_sha256': build_result['checker_sha256'], 'checker_unchanged': unchanged,
          'release_id': build_result['release_id'], 'product_executed': False, 'board_accessed': False,
          'scope': 'ARM64 ELF and archived dependency inspection only'}
with (here / 'checker_dependency_check_result.json').open('x') as stream:
    json.dump(record, stream, indent=2)
    stream.write('\n')
print(json.dumps(record))
sys.exit(result.returncode if unchanged else 5)
