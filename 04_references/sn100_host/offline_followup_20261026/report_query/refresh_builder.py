"""Generate only the private compiler driver; no verifier/compiler/GUI is run."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
WORK = Path(__file__).resolve().parent
BASE = ROOT / '06_programs/sn100_host/tests/build_shell_drivers.py'
text = BASE.read_text(encoding='utf-8')

def once(old, new):
    global text
    if text.count(old) != 1:
        raise RuntimeError('Expected one reviewed compiler-driver anchor: ' + old[:100])
    text = text.replace(old, new, 1)

once('import subprocess\n', 'import subprocess\nfrom native_binding import verify_native_binding, verify_full_source_inventory\n')
once("    parser.add_argument('--snapshot', type=Path, required=True)\n", """    parser.add_argument('--snapshot', type=Path, required=True)
    parser.add_argument('--snapshot-sha256', required=True)
    parser.add_argument('--native-verification', type=Path, required=True)
    parser.add_argument('--native-verification-sha256', required=True)
    parser.add_argument('--renderer', type=Path, required=True)
""")
wsl = '/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux'
once('    program = Path(__file__).resolve().parents[1]\n',
     '    program = Path(' + repr(wsl + '/06_programs/sn100_host') + ')\n')
once('    before = json.loads(args.snapshot.read_text())\n', """    if args.drivers != ['integrated_report_test'] or args.reuse_dir:
        parser.error('This private driver only builds the fresh diagnostic test copy')
    binding_args = dict(program=program, snapshot=args.snapshot, snapshot_sha256=args.snapshot_sha256,
        build=args.build_dir, verification=args.native_verification,
        verification_sha256=args.native_verification_sha256, renderer=args.renderer)
    binding_before = verify_native_binding(**binding_args)
    prepared = json.loads((Path(__file__).parent / 'prepared_sources.json').read_text())
    before = json.loads(args.snapshot.read_text())
""")
once("""    def check_sources():
        for name, expected in before.items():
            if sha(program / name) != expected:
                raise RuntimeError('Source changed: ' + name)
""", """    def check_sources():
        verify_full_source_inventory(program, before)
""")
once("        source = (program / independent.get(name, 'tests/' + name + '.cpp')).resolve(strict=True)\n",
     '        source = Path(' + repr(wsl + '/temp/sn100_host/offline_followup_20261026/report_query/source/integrated_report_test.cpp') + ').resolve(strict=True)\n' + """        if (sha(source) != prepared['trace_sha256']
                or sha(source.parent.parent / 'report_query_trace.h') != prepared['trace_header_sha256']):
            raise RuntimeError('Prepared diagnostic copy or header changed')
""")
text = text.replace('source.relative_to(program).as_posix()', 'source.as_posix()')
once('    build.mkdir(parents=True, exist_ok=True)\n',
     '    build.mkdir(parents=True, exist_ok=False)\n')
once("    proof = {'native_makefile_sha256': sha(native / 'Makefile'), 'snapshot_sha256': sha(args.snapshot),\n",
     """    binding_after = verify_native_binding(**binding_args)
    if binding_after != binding_before:
        raise RuntimeError('Supplied native binding changed during driver compilation')
    proof = {'supplied_native_binding': binding_before,
             'native_makefile_sha256': sha(native / 'Makefile'), 'snapshot_sha256': sha(args.snapshot),
""")
output = WORK / 'build_probe.py'
output.write_text(text, encoding='utf-8', newline='\n')

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

metadata = WORK / 'prepared_sources.json'
if metadata.exists():
    prepared = json.loads(metadata.read_text(encoding='utf-8'))
    prepared.update(base_builder_sha256=sha(BASE), prepared_builder_sha256=sha(output),
                    native_binding_sha256=sha(WORK / 'native_binding.py'),
                    runner_sha256=sha(WORK / 'run_queue.py'), private_binding_revision=2,
                    compile_or_gui_executed=False)
    metadata.write_text(json.dumps(prepared, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print('Regenerated explicit-binding private driver only; no native verifier/build/GUI executed')
