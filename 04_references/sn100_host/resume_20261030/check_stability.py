"""源码和归档检查，不执行产品或功能测试。"""
import hashlib
import importlib.util
import json
import pathlib

work = pathlib.Path(__file__).resolve().parent
root = work.parents[2]
program = root / '06_programs/sn100_host'
spec = importlib.util.spec_from_file_location('package', program / 'shell/package_arm64.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
frozen = json.loads((work / 'source_snapshot.json').read_text())
current = module.snapshot(program)
product_diff = [name for name in sorted(set(frozen) | set(current)) if frozen.get(name) != current.get(name)]
all_frozen = json.loads((work / 'all_source_identity.json').read_text())
diffs = {name: {'frozen': digest, 'current': hashlib.sha256((program / name).read_bytes()).hexdigest()}
         for name, digest in all_frozen.items() if digest != hashlib.sha256((program / name).read_bytes()).hexdigest()}
quality = 'tests/original_quality_pages_rk_test.cpp'
allowed = {quality} | {name for name in diffs if name.endswith('.md')}
assert not product_diff, product_diff
assert set(diffs) <= allowed, diffs
assert hashlib.sha256((program / quality).read_bytes()).hexdigest() == hashlib.sha256((work / 'test_inputs/original_quality_pages_rk_test.cpp').read_bytes()).hexdigest()
report = {'formal_package_inputs': len(frozen), 'production_input_differences': product_diff,
          'all_frozen_file_differences': diffs, 'reason': '质控测试独立修订并重编，Markdown说明不参与构建；其他输入必须保持一致。'}
(work / 'source_stability.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print('PRODUCT_SOURCE_STABLE=1')
