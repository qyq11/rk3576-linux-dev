from pathlib import Path
import hashlib,json,subprocess,sys
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
wrapper=HERE/'sample_checker15_board_a_wrapper_01/prepare_and_build.py'
assert hashlib.sha256(wrapper.read_bytes()).hexdigest()=='b29b58316767bd12f8cef1e6265f3336f67b1ca10879029abac6f6b6079c3bbc'
def wsl(p):
    value=p.as_posix();assert value.startswith('C:/');return '/mnt/c/'+value[3:]
argv=['wsl.exe','-d','Ubuntu-24.04','-u','qyq','--','env','-u','PYTHONOPTIMIZE','python3','-B',wsl(wrapper),
    wsl(ROOT/'temp/sn100_host/workflow_next_20261103/integration_15_01'),
    'd4501a69e100adbd3e3b87a4f311533ed7cfdabdd69d44f124fd822a7add4e4d',
    wsl(ROOT/'temp/sn100_host/rk_prepare/sysroot'),'--namespace','sample_checker15_board_a_exec_01_01','--execute']
paths={s:HERE/('board_a_checker_build_01'+s) for s in ('.command.json','.stdout.log','.stderr.log','.exit')}
assert all(not p.exists() for p in paths.values())
with paths['.command.json'].open('x',encoding='utf-8') as f: json.dump({'argv':argv,'cwd':str(ROOT),'scope':'arm64_cross_build_only_board_a_identity_cpp_same_formal15_objects'},f,indent=2)
actual=subprocess.run(argv,cwd=ROOT,capture_output=True)
for s,raw in (('.stdout.log',actual.stdout),('.stderr.log',actual.stderr),('.exit',(str(actual.returncode)+'\n').encode())):
    with paths[s].open('xb') as f:f.write(raw)
print(json.dumps({'actual_exit':actual.returncode,'stdout_bytes':len(actual.stdout),'stderr_bytes':len(actual.stderr)}))
if actual.returncode:sys.stderr.buffer.write(actual.stderr)
raise SystemExit(actual.returncode)
