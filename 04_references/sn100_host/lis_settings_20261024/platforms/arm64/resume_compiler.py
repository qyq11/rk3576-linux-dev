from pathlib import Path
import hashlib,json,os,subprocess,sys
base=Path(__file__).resolve().parent
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
contract=json.loads((base/'resume_inputs.json').read_text())
argv=['/usr/bin/aarch64-linux-gnu-g++',*sys.argv[1:]]
output=argv[argv.index('-o')+1]
saved=contract['reusable'].get(output)
if saved:
    assert argv==saved['argv'] and str(Path.cwd())==saved['cwd']
    assert sha(output)==saved['output_sha256']
    assert sha(saved['source'])==saved['source_sha256']
    assert all(sha(path)==digest for path,digest in contract['generated_headers'].items())
    (base/'resume_reused').mkdir(exist_ok=True)
    (base/'resume_reused'/(Path(output).name+'.json')).write_text(json.dumps({'output':output,'verified_exact_argv':True,'original_receipt':saved['receipt_file'],'sha256':saved['output_sha256']})+'\n')
    raise SystemExit(0)
os.execv('/usr/bin/python3',['python3',str(base/'capture_compiler.py'),*sys.argv[1:]])
