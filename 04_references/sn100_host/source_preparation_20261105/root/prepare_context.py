"""核对既有697份正式源，准备候选所需头文件生成和编译参数。"""
from pathlib import Path
import hashlib
import importlib.util
import json
import subprocess

root = Path(__file__).resolve().parents[3]
work = Path(__file__).resolve().parent
program = root / '06_programs/sn100_host'
spec = importlib.util.spec_from_file_location('package_arm64', program / 'shell/package_arm64.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
actual = {key.replace('\\', '/'): value for key, value in module.snapshot(program).items()}
expected = json.loads((root / '07_backups/sn100_host/board_a/resume_20261104_02/source_snapshot.json').read_text())
assert actual == expected and len(actual) == 697
(work / 'formal_before.json').write_text(json.dumps(actual, indent=2) + '\n')
status = subprocess.run(['git', 'status', '--short'], cwd=root, capture_output=True, check=True)
(work / 'git_status_before.txt').write_bytes(status.stdout)
source = program / 'shell/build.sh'
text = source.read_text()
prefix = text.split('\nobjects=()\n', 1)[0]
assert len(prefix) < len(text) and 'compile_flags=(' in prefix
original = 'module_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)'
assert prefix.count(original) == 1
base = '/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux'
prefix = prefix.replace(original, f'module_dir={base}/06_programs/sn100_host/shell')
prefix += '''
python3 - "$SN100_CANDIDATE_FLAGS_OUTPUT" "$include" "$build_dir" --flags "${compile_flags[@]}" --sources "${sources[@]}" <<'PY'
import json,sys
from pathlib import Path
output,qt,generated,*args=sys.argv[1:]
split=args.index('--sources')
data={'qt_headers':qt,'generated':generated,'flags':args[1:split],'sources':args[split+1:]}
Path(output).write_text(json.dumps(data,indent=2)+'\\n')
print(json.dumps({'generated_headers':len(list(Path(generated).glob('ui_*.h'))),'source_units':len(data['sources']),'product_compiled':False,'product_run':False}))
PY
'''
(work / 'prepare_headers.sh').write_text(prefix, newline='\n')
report = {'formal_sources_verified': 697, 'formal_changed': False,
          'build_script_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
          'purpose': '复用正式脚本的参数与uic/moc/rcc生成阶段，停在编译循环前；不编译或运行产品，不联板。'}
(work / 'preparation_basis.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
print(json.dumps(report, ensure_ascii=False))
