"""只在电脑固定本轮源码并交叉编译；不执行产品或功能测试。"""
import hashlib
import importlib.util
import json
import pathlib
import shutil
import subprocess

root = pathlib.Path(__file__).resolve().parents[3]
work = pathlib.Path(__file__).resolve().parent
program = root / '06_programs/sn100_host'
cache = pathlib.Path('/home/qyq/.cache/sn100-reagent-calendar-20261030-01/06_programs/sn100_host')
cache.mkdir(parents=True, exist_ok=False)
names = subprocess.check_output(['git', 'ls-files', '-z', '06_programs/sn100_host'], cwd=root).decode().split('\0')
identity = {}
for name in filter(None, names):
    source = root / name
    relative = source.relative_to(program)
    target = cache / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    assert hashlib.sha256(target.read_bytes()).hexdigest() == digest
    identity[str(relative)] = digest
(work / 'all_source_identity.json').write_text(json.dumps(identity, indent=2) + '\n')
spec = importlib.util.spec_from_file_location('package', cache / 'shell/package_arm64.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
(work / 'source_snapshot.json').write_text(json.dumps(module.snapshot(cache), indent=2) + '\n')
(work / 'source_cache_path').write_text(str(cache) + '\n')
print('FROZEN_ALL_TRACKED_FILES=' + str(len(identity)), flush=True)
print('FROZEN_PACKAGE_FILES=' + str(len(module.snapshot(cache))), flush=True)
