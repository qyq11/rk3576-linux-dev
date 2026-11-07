"""仅交叉编译冻结候选CPP及头文件对应moc，不链接或运行产品。"""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import sys

work = Path(__file__).resolve().parent
root = work.parents[2]
kind = sys.argv[1]
context = json.loads((work / 'compile_context.json').read_text())
meta = json.loads((work / f'frozen_{kind}.json').read_text())
stage = work / 'compile_inputs' / kind
program = stage / '06_programs/sn100_host'
out = work / 'compiled' / kind
out.mkdir(parents=True, exist_ok=False)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
for rel, digest in meta['after_sha256'].items():
    assert sha(stage / rel) == digest
flags = ['-I' + str(program), *context['flags']]
sources = [stage / rel for rel in meta['source_files'] if rel.endswith('.cpp')]
for rel in meta['source_files']:
    if not rel.endswith('.h'): continue
    header = stage / rel
    if 'Q_OBJECT' not in header.read_text(): continue
    generated = out / ('moc_' + header.stem + '.cpp')
    args = ['/usr/lib/qt5/bin/moc', '-DNO_BARCODE', '-DHAVE_QT5', '-I' + context['qt_headers'],
            *[flag for flag in flags if flag.startswith('-I')], str(header), '-o', str(generated)]
    subprocess.run(args, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    sources.append(generated)
results = []
for source in sources:
    obj = out / (source.stem + '.o')
    command = ['aarch64-linux-gnu-g++', *flags, '-c', str(source), '-o', str(obj)]
    (out / (source.stem + '.command.json')).write_text(json.dumps(command, indent=2) + '\n')
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    (out / (source.stem + '.stdout')).write_bytes(result.stdout)
    (out / (source.stem + '.stderr')).write_bytes(result.stderr)
    item = {'source': str(source.relative_to(work)), 'source_sha256': sha(source), 'exit': result.returncode}
    results.append(item)
    (out / 'result.json').write_text(json.dumps({'units': results, 'product_linked': False, 'product_run': False}, indent=2) + '\n')
    if result.returncode:
        print(result.stderr.decode(errors='replace')[-3000:]); sys.exit(result.returncode)
    architecture = subprocess.run(['readelf', '-h', str(obj)], capture_output=True, check=True).stdout.decode()
    assert 'AArch64' in architecture
    item['object_sha256'] = sha(obj)
    item['architecture'] = 'AArch64'
    print(json.dumps(item), flush=True)
compiler = Path(shutil.which('aarch64-linux-gnu-g++')).resolve()
version = subprocess.run([str(compiler), '--version'], capture_output=True, check=True).stdout.decode().splitlines()[0]
report = {'kind': kind, 'units': results, 'compiler_sha256': sha(compiler), 'compiler_version': version,
          'flags_from_formal_build': True, 'flags_context_sha256': sha(work / 'compile_context.json'),
          'product_linked': False, 'product_run': False, 'board_accessed': False}
(out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
