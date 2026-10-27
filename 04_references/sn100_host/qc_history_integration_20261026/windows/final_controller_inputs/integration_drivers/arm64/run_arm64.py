#!/usr/bin/env python3
"""New frozen full ARM64 main/helper build and closure only; no packaging/deploy."""
import datetime
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import time

WORK = Path(__file__).resolve().parents[2]
ROOT = WORK.parents[2]
PROGRAM = ROOT / '06_programs/sn100_host'
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import verify_native
ARM = WORK / 'platforms/arm64'
BUILD = ARM / 'build'
SYSROOT = ROOT / 'temp/sn100_host/o02_login/platforms/arm64/sysroot'
FREEZE = WORK / 'native/final_before.json'
STEPS = []
RUN_OWNED = False
def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''):
            h.update(block)
    return h.hexdigest()
def now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()
def save(name, value):
    path = ARM / name
    temporary = path.with_name(path.name + '.writing')
    with temporary.open('w', encoding='utf-8') as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.write('\n')
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)
def readjson(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))
def require(condition, message):
    if not condition:
        raise RuntimeError(message)
def step(name, argv, env=None, pass_fds=(), timeout=180):
    record = {'name': name, 'argv': list(map(str, argv)), 'cwd': str(ROOT),
              'started_utc': now(), 'stdout_stderr': name + '.log', 'timeout_seconds': timeout}
    save(name + '_command.json', record)
    process = None
    failure = None
    cleanup_errors = []
    group_stopped = True
    def group_exists():
        try:
            os.killpg(process.pid, 0)
            return True
        except ProcessLookupError:
            return False
    try:
        with (ARM / (name + '.log')).open('wb') as stream:
            process = subprocess.Popen(record['argv'], cwd=ROOT, env=env, stdout=stream,
                stderr=subprocess.STDOUT, pass_fds=pass_fds, start_new_session=True)
            save(name + '_process.json', {'pid':process.pid, 'state':'running'})
            process.wait(timeout=timeout)
    except BaseException as error:
        failure = error
    finally:
        if process is not None:
            # A failed wait must not lose the original exit receipt. All signals
            # target only this Popen(start_new_session=True) process group.
            try:
                if failure is not None and group_exists():
                    os.killpg(process.pid, signal.SIGTERM)
                    try: process.wait(timeout=3)
                    except subprocess.TimeoutExpired: pass
                if group_exists():
                    os.killpg(process.pid, signal.SIGKILL)
                try: process.wait(timeout=10)
                except subprocess.TimeoutExpired as error: cleanup_errors.append(str(error))
                deadline = time.monotonic() + 10
                while group_exists() and time.monotonic() < deadline:
                    time.sleep(0.05)
                group_stopped = not group_exists()
            except BaseException as error:
                cleanup_errors.append(type(error).__name__ + ': ' + str(error))
                group_stopped = False
    record.update(owned_process_group_stopped=group_stopped, cleanup_errors=cleanup_errors,
                  timed_out=isinstance(failure, subprocess.TimeoutExpired), exit_code=process.returncode if process is not None else None,
                  finished_utc=now(), pid=process.pid if process is not None else None,
                  state='not_started' if process is None else ('finished' if process.returncode is not None else 'exit_unknown'))
    if failure: record.update(exception_type=type(failure).__name__, exception=str(failure))
    STEPS.append(record)
    save(name + '_exit.json', record)
    save(name + '_process.json', {'pid':record['pid'], 'state':record['state'], 'exit_code':record['exit_code']})
    if failure: raise failure
    require(record['exit_code'] == 0 and group_stopped and not cleanup_errors,
            name + ' failed or left an owned process group: ' + str(record['exit_code']))
def sysroot_inputs():
    original = readjson(ROOT / 'temp/sn100_host/qc_local_20261025/platforms/arm64/dependencies/dependency_report.json')
    require(original['offline_link_and_sql_closure_passed'] and not original['errors'],
            'Known read-only sysroot input inventory needs its historical successful closure')
    paths = set()
    # Runtime closure alone does not freeze the Qt headers consumed by C++.
    # Hash all actual Qt include inputs before/after the serial ARM build too.
    qt_include = SYSROOT / 'usr/include/aarch64-linux-gnu/qt5'
    require(qt_include.is_dir(), 'Existing Qt header tree is required; no restore allowed')
    paths.update(path for path in qt_include.rglob('*') if path.is_file())
    for row in original['files']:
        path = Path(row['path'])
        if path.is_relative_to(SYSROOT):
            paths.add(path)
    # All existing libraries in the two isolated link directories are read-only inputs.
    # The actual NEW closure must be a subset of this prebuild fingerprint.
    for relative in ('usr/lib/aarch64-linux-gnu', 'lib/aarch64-linux-gnu'):
        paths.update(path for path in (SYSROOT / relative).rglob('*') if path.is_file())
    for rel in ['report_private_libraries.txt',
                'usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc',
                'usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc',
                'usr/share/doc/fonts-noto-cjk/copyright',
                'usr/share/doc/libqt5gui5t64/copyright',
                'usr/share/doc/libqt5svg5/copyright',
                'usr/share/doc/libfontconfig1/copyright',
                'usr/share/doc/libexpat1/copyright']:
        paths.add(SYSROOT / rel)
    values = {}
    for path in sorted(paths):
        resolved = path.resolve(strict=True)
        require(resolved.is_relative_to(SYSROOT), 'Sysroot symlink escape: ' + str(path))
        values[path.relative_to(SYSROOT).as_posix()] = {
            'sha256': digest(resolved), 'symlink': os.readlink(path) if path.is_symlink() else None}
    return values
def audit_objects(start_ns, receipts):
    rows = [line.split('\t', 1) for line in (ARM / 'actual_input_list.tsv').read_text().splitlines()]
    formal = [Path(value) for role, value in rows if role == 'source']
    headers = [Path(value) for role, value in rows if role == 'header']
    for relative in ('quality_control/qc_store.cpp', 'quality_control/qc_history_core.cpp',
                     'quality_control/qc_history_presenter.cpp', 'quality_control/cus_quality_chart.cpp',
                     'quality_control/qualitychart.cpp', 'quality/statistics.cpp'):
        require(formal.count(PROGRAM / relative) == 1, 'Expected exactly one fresh QC source unit: ' + relative)
    generated_moc = [BUILD / ('moc_' + p.stem + '.cpp') for p in headers if b'Q_OBJECT' in p.read_bytes()]
    # Follow the exact frozen compiler receipts so additions to the formal
    # resource list are audited without a stale hardcoded QRC-name inventory.
    generated_qrc = [Path(receipts[str(BUILD / ('obj_' + str(n) + '.o'))]['source'])
                     for n in range(len(formal) + len(generated_moc),
                                    len(formal) + len(generated_moc) + len([1 for role, _ in rows if role == 'resource']))]
    require(all(path.parent == BUILD and path.name.startswith('qrc_') and path.suffix == '.cpp'
                for path in generated_qrc), 'Generated resource source identity mismatch')
    ordered = formal + generated_moc + generated_qrc
    exclusive_helpers = [Path(value) for role, value in rows if role == 'helper_source'
                         and Path(value).name in {'report_renderer.cpp', 'report_pdf_mapping.cpp'}]
    require(len(exclusive_helpers) == 2, 'Expected two exclusive report helper units')
    expected = [(BUILD / ('obj_' + str(n) + '.o'), path) for n, path in enumerate(ordered)]
    expected.extend((BUILD / ('helper_' + path.stem + '.o'), path) for path in exclusive_helpers)
    actual = set(BUILD.glob('obj_*.o')) | set(BUILD.glob('helper_*.o'))
    require(actual == {obj for obj, path in expected}, 'Fresh ARM object set differs from formal generated sequence')
    compiler = (ARM / 'compiler_version.log').read_text().splitlines()[0].strip()
    records = []
    for obj, source in expected:
        receipt = receipts.get(str(obj))
        require(receipt is not None and receipt['source'] == str(source) and receipt['output_sha256'] == digest(obj), 'Exact compile receipt/source/rawSHA mismatch: ' + str(obj))
        require(all(flag in receipt['argv'] for flag in ['-O2','-g','-std=c++14','-DQT_NO_DEBUG','-fPIC']), 'Exact compile receipt flags mismatch: ' + str(obj))
        require(obj.stat().st_mtime_ns > start_ns, 'Object predates this fresh build: ' + str(obj))
        audit_name = 'object_' + str(len(records)) + '_readelf'
        step(audit_name, ['aarch64-linux-gnu-readelf', '-h', '--debug-dump=info', '--dwarf-depth=1', str(obj)], timeout=60)
        text = (ARM / (audit_name + '.log')).read_text()
        require(re.search(r'Class:\s+ELF64', text) and re.search(r'Machine:\s+AArch64', text)
                and re.search(r'Type:\s+REL', text), 'Wrong object ELF: ' + str(obj))
        attributes = {}
        for line in text.splitlines():
            for key in ['DW_AT_name', 'DW_AT_comp_dir', 'DW_AT_producer', 'DW_AT_language']:
                if key not in attributes and key in line:
                    attributes[key] = line.rsplit(': ', 1)[-1].strip()
        actual_source = Path(attributes['DW_AT_name'])
        if not actual_source.is_absolute():
            actual_source = Path(attributes['DW_AT_comp_dir']) / actual_source
        require(actual_source.resolve() == source.resolve(), 'DWARF source mismatch: ' + str(obj))
        producer = attributes['DW_AT_producer']
        require(compiler in producer and all(flag in producer for flag in ['-O2', '-g', '-std=c++14']),
                'Unexpected object compiler/flags: ' + str(obj))
        records.append({'object': obj.relative_to(ARM).as_posix(), 'source': str(source),
                        'object_sha256': digest(obj), 'source_sha256': digest(source),
                        'created_after_prebuild': True, 'readelf_exit_code': 0, 'readelf_receipt': audit_name + '_exit.json',
                        'elf': 'ELF64 REL AArch64', 'dwarf': attributes, 'compiler_receipt': receipt['receipt_file'], 'actual_compile_argv': receipt['argv']})
    report = {'formal_units': len(formal), 'generated_moc_units': len(generated_moc),
              'generated_qrc_units': len(generated_qrc), 'main_objects': len(ordered),
              'helper_exclusive_objects': len(exclusive_helpers), 'total_objects': len(expected),
              'all_sources_and_producer_flags_verified': True, 'objects': records}
    require(set(receipts) == {str(obj) for obj, source in expected}, 'Compile receipt set differs from all fresh main/helper units')
    save('dwarf_object_sources.json', report)
    return {k: v for k, v in report.items() if k != 'objects'}
def audit_compiler_receipts():
    rows = []
    compiled = {}
    links = []
    for path in sorted((ARM / 'compiler_receipts').glob('*.json')):
        row = readjson(path)
        require(row.get('exit_code') == 0 and row['compiler'] == '/usr/bin/aarch64-linux-gnu-g++' and row['cwd'] == str(ROOT), 'Compiler receipt failure or identity mismatch: ' + str(path))
        row['receipt_file'] = path.relative_to(ARM).as_posix()
        rows.append(row)
        if 'source' in row:
            require(row['output'] not in compiled, 'Duplicate compiler output receipt')
            compiled[row['output']] = row
        else:
            links.append(row)
    require(len(links) == 2 and {row['output'] for row in links} == {str(BUILD / 'sn100_shell.new'), str(BUILD / 'sn100_report_renderer.new')}, 'Expected exactly two complete main/helper link argv receipts')
    require(all(row['output_sha256'] == digest(Path(row['output']).with_suffix('')) for row in links), 'Link receipt/final binary SHA mismatch')
    save('compiler_receipt_summary.json', {'total_invocations':len(rows), 'compile_invocations':len(compiled), 'link_invocations':len(links), 'all_exit_codes_zero':True, 'parallel_xtrace_is_not_argv_proof':True, 'links':links})
    return compiled

def audit_link_sources(compiled):
    """Pair exact main/helper linker argv with the freshly compiled source map."""
    rows = [line.split('\t', 1) for line in (ARM / 'actual_input_list.tsv').read_text().splitlines()]
    declared_helper = {value for role, value in rows if role == 'helper_source'}
    vendor_mocs = {str(BUILD / ('moc_' + Path(value).stem + '.cpp'))
                   for role, value in rows if role == 'header'
                   and Path(value).is_relative_to(PROGRAM / 'thirdparty/qtrpt')
                   and b'Q_OBJECT' in Path(value).read_bytes()}
    allowed_helper_generated = vendor_mocs | {str(BUILD / name) for name in
        ('moc_history_store.cpp', 'qrc_report_resources.cpp', 'qrc_imagesRpt.cpp')}
    main_objects = sorted((name for name in compiled if Path(name).name.startswith('obj_')),
                          key=lambda name: int(Path(name).stem.split('_')[1]))
    report = []
    for link in readjson(ARM / 'compiler_receipt_summary.json')['links']:
        objects = [argument for argument in link['argv'][1:] if argument.endswith('.o')]
        require(len(objects) == len(set(objects)) and all(obj in compiled for obj in objects),
                'Link has duplicate or unrecorded objects: ' + link['output'])
        sources = {compiled[obj]['source'] for obj in objects}
        if Path(link['output']).name == 'sn100_shell.new':
            require(objects == main_objects, 'Main link differs from exact fresh main object sequence')
            role = 'main'
        else:
            require(declared_helper <= sources and sources <= declared_helper | allowed_helper_generated,
                    'Helper link sources differ from declared report-only closure')
            require(vendor_mocs <= sources, 'Helper omitted a generated vendor QObject unit')
            role = 'renderer'
        report.append({'role': role, 'output': link['output'], 'output_sha256': link['output_sha256'],
                       'compiler_receipt': link['receipt_file'], 'objects': [
                           {'object': obj, 'source': compiled[obj]['source'],
                            'object_sha256': compiled[obj]['output_sha256']} for obj in objects]})
    save('link_source_map.json', {'links': report, 'paired_fresh_main_and_renderer': True})

def main():
    global RUN_OWNED, ARM, BUILD
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root-go', action='store_true', required=True)
    parser.add_argument('--attempt', default='arm64')
    args = parser.parse_args()
    require(args.root_go and re.fullmatch(r'arm64(?:_[A-Za-z0-9_-]+)?', args.attempt), 'Explicit root go and safe fresh ARM attempt name required')
    ARM = WORK / 'platforms' / args.attempt
    BUILD = ARM / 'build'
    require(os.geteuid() != 0, 'Run as ordinary WSL user qyq')
    preparation = readjson(WORK / 'integration_drivers/platform_preparation.json')
    require(FREEZE.is_file(), 'Explicit new source freeze required')
    frozen = readjson(FREEZE)
    freeze_sha256 = digest(FREEZE)
    require(freeze_sha256 == preparation['freeze']['sha256'] and len(frozen) == preparation['freeze']['entries'], 'Wrong explicit QC-history freeze')
    native = verify_native(frozen)  # Requires the completed NEW proof, Makefile, pair and fresh objects.
    native_sha256 = digest(WORK / 'native/verification.json')
    for row in preparation['controller_inputs']:
        require(digest(WORK / row['path']) == row['sha256'], 'Prepared controller changed: ' + row['path'])
    for row in preparation['prior_passed_receipts']:
        require(digest(ROOT / row['path']) == row['sha256'], 'Historical input receipt changed: ' + row['path'])
    require(not ARM.exists(), 'Fresh ARM build/evidence root required; preserve existing failures')
    ARM.mkdir(parents=True)
    RUN_OWNED = True
    preparation_path = WORK / 'integration_drivers/platform_preparation.json'
    preparation_sha256 = digest(preparation_path)
    shutil.copyfile(preparation_path, ARM / 'platform_preparation_input.json')
    save('controller_inputs_before.json', preparation['controller_inputs'])
    require(frozen and all(digest(PROGRAM / name) == value for name, value in frozen.items()), 'Frozen source inputs must all match before starting')
    for name in ['quality_control/qc_store.cpp', 'quality_control/quality_control_page.cpp',
                 'calibration/calibration_store.cpp', 'calibration/calibration_page.cpp',
                 'quality_control/qc_history_core.cpp', 'quality_control/qc_history_reply.h',
                 'quality_control/qc_history_presenter.cpp', 'quality/statistics.cpp']:
        require(name in frozen, 'Current four-window source absent from freeze: ' + name)
    shutil.copyfile(Path(__file__).with_name('capture_compiler.py'), ARM / 'capture_compiler.py')
    save('native_binding_before.json', {'verification_sha256': native_sha256, 'snapshot_sha256': freeze_sha256, 'source_entries': len(frozen)})
    save('started.json', {'started_utc': now(), 'source_freeze': str(FREEZE), 'source_entries': len(frozen),
                          'sysroot_reused_read_only': str(SYSROOT), 'no_restore_copy_or_remote_actions': True})
    env = dict(os.environ)
    for name in ['SN100_ARM64_OBJECT_REUSE_MANIFEST', 'SN100_ARM64_FROZEN_SNAPSHOT', 'SN100_ARM64_REUSE_CPP',
                 'BASH_ENV', 'BASH_XTRACEFD']:
        env.pop(name, None)
    env.update(SN100_ARM64_SYSROOT=str(SYSROOT), SN100_ARM64_BUILD_DIR=str(BUILD),
               SN100_JOBS='2', RK_BUILD_TYPE='Release', SN100_ARM64_LIST_INPUTS='0')
    # Avoid build.sh's fallback restore path: missing baseline is an explicit failure.
    for rel in ['usr/lib/aarch64-linux-gnu/libQt5Sql.so.5.15.13',
                'usr/lib/aarch64-linux-gnu/libQt5Charts.so.5.15.13',
                'usr/include/aarch64-linux-gnu/qt5/QtSvg/QSvgRenderer',
                'usr/lib/aarch64-linux-gnu/libQt5Svg.so']:
        require((SYSROOT / rel).is_file(), 'Required existing sysroot input missing; no restore allowed: ' + rel)
    sysroot_before = sysroot_inputs()
    save('sysroot_before.json', sysroot_before)
    tools = [Path('/usr/bin/aarch64-linux-gnu-g++'), Path('/usr/bin/aarch64-linux-gnu-ld'),
             Path('/usr/bin/aarch64-linux-gnu-as'), Path('/usr/bin/aarch64-linux-gnu-readelf'),
             Path('/usr/lib/qt5/bin/moc'), Path('/usr/lib/qt5/bin/uic'), Path('/usr/lib/qt5/bin/rcc')]
    tool_before = {str(path): {'resolved':str(path.resolve(strict=True)), 'sha256':digest(path.resolve(strict=True))}
                   for path in tools}
    save('toolchain_before.json', tool_before)
    step('compiler_version', ['aarch64-linux-gnu-g++', '-dumpfullversion'])
    for name, argv in [('compiler_banner', ['aarch64-linux-gnu-g++', '--version']),
                       ('linker', ['aarch64-linux-gnu-ld', '--version']),
                       ('moc', ['/usr/lib/qt5/bin/moc', '-v']),
                       ('uic', ['/usr/lib/qt5/bin/uic', '-v']),
                       ('rcc', ['/usr/lib/qt5/bin/rcc', '-v'])]:
        step(name + '_version', argv)
    step('snapshot_before', ['python3', '-B', str(PROGRAM / 'shell/package_arm64.py'), 'snapshot',
                             '--output', str(ARM / 'source_before.json')])
    require(readjson(ARM / 'source_before.json') == frozen, 'Formal sources differ from native freeze')
    listed_env = dict(env, SN100_ARM64_LIST_INPUTS='1')
    step('list_inputs', ['bash', str(PROGRAM / 'shell/build.sh'), str(BUILD / 'sn100_shell')], env=listed_env)
    shutil.copyfile(ARM / 'list_inputs.log', ARM / 'actual_input_list.tsv')
    save('prebuild_inputs.json', {'source_freeze_sha256': digest(FREEZE), 'source_entries': len(frozen),
                                  'formal_build_script_sha256': digest(PROGRAM / 'shell/build.sh'),
                                  'sysroot': str(SYSROOT), 'build': str(BUILD),
                                  'build_environment': {k: env[k] for k in ['SN100_ARM64_SYSROOT','SN100_ARM64_BUILD_DIR','SN100_JOBS','RK_BUILD_TYPE']}})
    trace_source = ARM / 'trace_build.bash'
    trace_source.write_text('if [[ $0 == */sn100_host/shell/build.sh ]]; then\nfunction aarch64-linux-gnu-g++() { /usr/bin/python3 "' + str(ARM / 'capture_compiler.py') + '" "$@"; }\nPS4="+ "; set -x\nfi\n', encoding='utf-8')
    start_ns = time.time_ns()
    with (ARM / 'actual_commands.log').open('wb') as trace:
        build_env = dict(env, BASH_ENV=str(trace_source), BASH_XTRACEFD=str(trace.fileno()))
        step('build', ['bash', str(PROGRAM / 'shell/build.sh'), str(BUILD / 'sn100_shell')],
             env=build_env, pass_fds=(trace.fileno(),), timeout=2700)
    step('snapshot_after_build', ['python3','-B',str(PROGRAM/'shell/package_arm64.py'),'snapshot',
                                  '--output',str(ARM/'source_after_build.json')])
    require(readjson(ARM/'source_after_build.json') == frozen, 'Sources changed during full ARM build')
    receipts = audit_compiler_receipts()
    object_summary = audit_objects(start_ns, receipts)
    audit_link_sources(receipts)
    step('closure', ['python3','-B',str(PROGRAM/'shell/check_arm64.py'),str(BUILD/'sn100_shell'),
                     '--additional-binary',str(BUILD/'sn100_report_renderer'),'--sysroot',str(SYSROOT),
                     '--output-dir',str(ARM/'dependencies')])
    closure = readjson(ARM/'dependencies/dependency_report.json')
    require(closure['offline_link_and_sql_closure_passed'] and not closure['errors']
            and not closure['board_runtime_verified'], 'Offline closure did not pass')
    require(digest(FREEZE) == freeze_sha256, 'Explicit freeze changed during ARM build')
    sysroot_after = sysroot_inputs()
    save('sysroot_after.json', sysroot_after)
    require(sysroot_after == sysroot_before, 'Reused sysroot inputs changed')
    tool_after = {str(path): {'resolved':str(path.resolve(strict=True)), 'sha256':digest(path.resolve(strict=True))}
                  for path in tools}
    save('toolchain_after.json', tool_after)
    require(tool_after == tool_before, 'ARM compiler or Qt generator input changed during build')
    require(digest(WORK / 'native/verification.json') == native_sha256, 'Native proof changed during ARM build')
    require(digest(preparation_path) == preparation_sha256, 'Platform preparation changed during ARM build')
    for row in preparation['controller_inputs']:
        require(digest(WORK / row['path']) == row['sha256'], 'Controller input changed during ARM build: ' + row['path'])
    save('controller_inputs_after.json', preparation['controller_inputs'])
    verify_native(frozen)
    save('native_binding_after.json', {'verification_sha256': native_sha256, 'snapshot_sha256': freeze_sha256, 'source_entries': len(frozen)})
    for row in closure['files']:
        path = Path(row['path'])
        if path.is_relative_to(SYSROOT):
            key = path.relative_to(SYSROOT).as_posix()
            require(key in sysroot_before and row['sha256'] == sysroot_before[key]['sha256'], 'New closure input lacked a matching prebuild fingerprint: ' + str(path))
    result = {'schema':'sn100-qc-history-arm64-build-offline-v1','finished_utc':now(),'source_entries':len(frozen),
              'source_before_after_equal_native_freeze':True,
              'build_exit_code':0,'closure_exit_code':0,'packaging_performed':False,
              'sysroot_reused_read_only':str(SYSROOT),'sysroot_fingerprint_unchanged':True,
              'full_object_recompilation':True,'object_summary':object_summary,
              'source_freeze_sha256':freeze_sha256,'toolchain_fingerprints_unchanged':True,
              'main_sha256':digest(BUILD/'sn100_shell'),'renderer_sha256':digest(BUILD/'sn100_report_renderer'),
              'closure_elf_count':len(closure['files']),'closure_errors':closure['errors'],
              'native_verification_sha256':native_sha256,'preparation_sha256':preparation_sha256,
              'link_source_map':'link_source_map.json',
              'board_runtime_verified':False,'real_printer_or_instrument_used':False,
              'formal_source_or_shared_document_written':False,'old_evidence_modified':False, 'exact_per_invocation_receipts':True, 'parallel_xtrace_is_not_argv_proof':True}
    save('steps.json', STEPS)
    save('verification.json', result)
    print(json.dumps(result, ensure_ascii=False))
if __name__ == '__main__':
    try:
        main()
    except BaseException as error:
        if RUN_OWNED:
            save('steps.json', STEPS)
            save('failure.json', {'failed_utc':now(),'error':str(error),'error_type':type(error).__name__,
                                  'completed_steps':STEPS,'no_previous_outputs_removed_or_overwritten':True})
        raise
