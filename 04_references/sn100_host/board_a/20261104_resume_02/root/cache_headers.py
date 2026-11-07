"""核对每份ARM64头文件后，使用WSL缓存加快重复include。"""
from pathlib import Path
import hashlib
import json
import os
import shutil

work = Path(__file__).resolve().parent
source = work / 'sysroot/usr/include/aarch64-linux-gnu/qt5'
cache = Path('/home/qyq/.cache/sn100_arm64_headers/resume_20261104_02')
assert source.is_dir() and not source.is_symlink() and not cache.exists()
shutil.copytree(source, cache)
def inventory(folder):
    return {str(p.relative_to(folder)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(folder.rglob('*')) if p.is_file()}
before, after = inventory(source), inventory(cache)
assert before == after
result = {'files': len(before), 'mismatches': 0, 'source': str(source), 'cache': str(cache)}
source.rename(source.with_name('qt5.archived'))
source.symlink_to(cache, target_is_directory=True)
(work / 'header_cache_verification.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result))
