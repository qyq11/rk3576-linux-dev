#!/usr/bin/env python3
"""Four-window isolated full ARM64 build/static packaging; explicit root go only."""
import datetime
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shlex
import shutil
import signal
import subprocess
import sys
import tarfile
import time

ROOT = Path('/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux')
PROGRAM = ROOT / '06_programs/sn100_host'
WORK = ROOT / 'temp/sn100_host/qc_local_20261025'
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
def step(name, argv, env=None, pass_fds=()):
    record = {'name': name, 'argv': list(map(str, argv)), 'cwd': str(ROOT),
              'started_utc': now(), 'stdout_stderr': name + '.log'}
    save(name + '_command.json', record)
    process = None
    failure = None
    try:
        with (ARM / (name + '.log')).open('wb') as stream:
            process = subprocess.Popen(record['argv'], cwd=ROOT, env=env, stdout=stream,
                stderr=subprocess.STDOUT, pass_fds=pass_fds, start_new_session=True)
            save(name + '_process.json', {'pid':process.pid, 'state':'running'})
            process.wait()
    except BaseException as error:
        failure = error
        if process is not None:
            try: os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError: pass
            try: process.wait(timeout=3)
            except subprocess.TimeoutExpired: pass
            try: os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError: pass
            process.wait()
    record.update(exit_code=process.returncode if process is not None else None,
                  finished_utc=now(), pid=process.pid if process is not None else None,
                  state='finished' if process is not None else 'not_started')
    if failure: record.update(exception_type=type(failure).__name__, exception=str(failure))
    STEPS.append(record)
    save(name + '_exit.json', record)
    save(name + '_process.json', {'pid':record['pid'], 'state':record['state'], 'exit_code':record['exit_code']})
    if failure: raise failure
    require(record['exit_code'] == 0, name + ' failed with exit ' + str(record['exit_code']))
def sysroot_inputs():
    original = readjson(ROOT / '04_references/sn100_host/password_login_20261024/platforms/arm64/final_dependencies/dependency_report.json')
    require(original['offline_link_and_sql_closure_passed'] and not original['errors'],
            'Old sysroot must have a successful recorded closure')
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
        process = subprocess.run(['aarch64-linux-gnu-readelf', '-h', '--debug-dump=info',
                                  '--dwarf-depth=1', str(obj)], stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE, text=True, check=False)
        require(process.returncode == 0 and not process.stderr, 'readelf failed: ' + str(obj))
        text = process.stdout
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
                        'created_after_prebuild': True, 'readelf_exit_code': process.returncode,
                        'elf': 'ELF64 REL AArch64', 'dwarf': attributes, 'compiler_receipt': receipt['receipt_file'], 'actual_compile_argv': receipt['argv']})
    report = {'formal_units': len(formal), 'generated_moc_units': len(generated_moc),
              'generated_qrc_units': len(generated_qrc), 'main_objects': len(ordered),
              'helper_exclusive_objects': len(exclusive_helpers), 'total_objects': len(expected),
              'all_sources_and_producer_flags_verified': True, 'objects': records}
    require(set(receipts) == {str(obj) for obj, source in expected}, 'Compile receipt set differs from all fresh main/helper units')
    save('dwarf_object_sources.json', report)
    return {k: v for k, v in report.items() if k != 'objects'}
def audit_package(frozen):
    package = Path((ARM / 'packages/latest_package').read_text().strip())
    require(package.is_relative_to(ARM / 'packages') and package.is_file(), 'Unexpected package location')
    members = {}
    with tarfile.open(package, 'r:gz') as bundle:
        for entry in bundle.getmembers():
            require(not PurePosixPath(entry.name).is_absolute() and '..' not in PurePosixPath(entry.name).parts,
                    'Unsafe archive member')
            require(entry.isdir() or entry.isfile(), 'Unexpected nonregular package member')
            if entry.isfile():
                require(entry.name not in members, 'Duplicate regular package member')
                data = bundle.extractfile(entry).read()
                members[entry.name] = {'sha256': hashlib.sha256(data).hexdigest(), 'size': len(data)}
        manifest_data = bundle.extractfile('SHA256SUMS').read()
        release_id = bundle.extractfile('release_id').read().decode('ascii').strip()
        release = json.loads(bundle.extractfile('release.json').read())
    manifest = dict((name, sha) for sha, name in [line.split('  ', 1) for line in manifest_data.decode('ascii').splitlines()])
    require(set(members) == set(manifest) | {'SHA256SUMS', 'release_id'}, 'Package exact member set mismatch')
    require(all(members[name]['sha256'] == sha for name, sha in manifest.items()), 'Package member SHA mismatch')
    require(hashlib.sha256(manifest_data).hexdigest() == release_id, 'Package release ID mismatch')
    require(package.name == 'sn100_shell_' + release_id + '.tar.gz', 'Archive filename/release mismatch')
    require(release['sources'] == frozen and release['board_runtime_verified'] is False,
            'Package source/runtime identity mismatch')
    require('local_lis_settings' in release['capabilities'] and '--lis-settings' in release['startup_feature_arguments'] and not release['lis_settings_scope']['communication'] and not release['lis_settings_scope']['serial_device_open'] and release['lis_settings_scope']['auto_flags'] == 'intent_only', 'LIS local-only package feature/scope mismatch')
    require('local_printer_settings' in release['capabilities']
            and '--printer-settings' in release['startup_feature_arguments'], 'Printer feature absent from package')
    require('quality_control_local' in release['capabilities']
            and '--quality-control' in release['startup_feature_arguments'], 'QC feature absent from package')
    require('calibration_local' in release['capabilities']
            and '--calibration' in release['startup_feature_arguments'], 'Calibration feature absent from package')
    require(not release['printer_settings_scope']['print_job_submitted']
            and not release['printer_settings_scope']['system_default_changed'], 'Package safety boundary mismatch')
    require(members['sn100_shell']['sha256'] == digest(BUILD / 'sn100_shell')
            and members['sn100_report_renderer']['sha256'] == digest(BUILD / 'sn100_report_renderer'),
            'Package main/helper differ from fresh checked pair')
    result = {'package': str(package), 'package_sha256': digest(package), 'release_id': release_id,
              'regular_files': len(members), 'manifest_entries': len(manifest),
              'all_member_sha256_verified': True, 'source_entries': len(frozen),
              'sources_equal_native_freeze': True, 'board_runtime_verified': False, 'members': members}
    save('package_member_audit.json', result)
    return {k: v for k, v in result.items() if k != 'members'}
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

def make_source_package(frozen, release_id):
    archive = ARM / 'packages' / ('sn100_source_' + release_id + '.tar.gz')
    require(not archive.exists(), 'Source package must be new')
    with tarfile.open(archive, 'w:gz', dereference=True) as bundle:
        for name, value in frozen.items():
            relative = PurePosixPath(name)
            path = PROGRAM / name
            require(not relative.is_absolute() and '..' not in relative.parts and path.resolve().is_relative_to(PROGRAM.resolve()) and path.is_file() and not path.is_symlink(), 'Unsafe source package input: ' + name)
            require(path.suffix.lower() not in ['.db','.sqlite','.ini'] and not any(part.lower() in ['runtime','secret','secrets'] for part in relative.parts), 'Runtime/credential-like input excluded: ' + name)
            require(digest(path) == value, 'Source package input changed: ' + name)
            bundle.add(path, arcname=name, recursive=False)
    with tarfile.open(archive, 'r:gz') as bundle:
        members = bundle.getmembers()
        require(len(members) == len(frozen) and {row.name for row in members} == set(frozen), 'Source package exact frozen member identity mismatch')
        require(all(row.isfile() and hashlib.sha256(bundle.extractfile(row).read()).hexdigest() == frozen[row.name] for row in members), 'Source package member SHA mismatch')
    result = {'path':str(archive), 'sha256':digest(archive), 'bytes':archive.stat().st_size, 'members':len(frozen), 'all_source_sha256_verified':True, 'runtime_data_or_credentials_archived':False}
    save('source_package_audit.json', result)
    return result

def main():
    global RUN_OWNED
    require(sys.argv[1:] == ['--root-go'], 'Explicit root go argument required')
    require(os.geteuid() != 0, 'Run as ordinary WSL user qyq')
    require(FREEZE.is_file(), 'Completed native freeze required')
    require(not ARM.exists(), 'Fresh ARM build/evidence root required; preserve existing failures')
    ARM.mkdir(parents=True)
    RUN_OWNED = True
    frozen = readjson(FREEZE)
    freeze_sha256 = digest(FREEZE)
    native = readjson(WORK / 'native/verification.json')
    require(native.get('passed') is True and native.get('actual_exit') == 0
            and native.get('snapshot_sha256') == freeze_sha256
            and native.get('source_inputs') == len(frozen), 'Completed native receipt differs from frozen source')
    require(frozen and all(digest(PROGRAM / name) == value for name, value in frozen.items()), 'Frozen source inputs must all match before starting')
    for name in ['quality_control/qc_store.cpp', 'quality_control/quality_control_page.cpp',
                 'calibration/calibration_store.cpp', 'calibration/calibration_page.cpp']:
        require(name in frozen, 'Current four-window source absent from freeze: ' + name)
    shutil.copyfile(Path(__file__).with_name('capture_compiler.py'), ARM / 'capture_compiler.py')
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
    step('tool_versions', ['bash', '-c', 'aarch64-linux-gnu-g++ --version; aarch64-linux-gnu-ld --version; /usr/lib/qt5/bin/moc -v; /usr/lib/qt5/bin/uic -v; /usr/lib/qt5/bin/rcc -v'])
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
             env=build_env, pass_fds=(trace.fileno(),))
    step('snapshot_after_build', ['python3','-B',str(PROGRAM/'shell/package_arm64.py'),'snapshot',
                                  '--output',str(ARM/'source_after_build.json')])
    require(readjson(ARM/'source_after_build.json') == frozen, 'Sources changed during full ARM build')
    receipts = audit_compiler_receipts()
    object_summary = audit_objects(start_ns, receipts)
    step('closure', ['python3','-B',str(PROGRAM/'shell/check_arm64.py'),str(BUILD/'sn100_shell'),
                     '--additional-binary',str(BUILD/'sn100_report_renderer'),'--sysroot',str(SYSROOT),
                     '--output-dir',str(ARM/'dependencies')])
    closure = readjson(ARM/'dependencies/dependency_report.json')
    require(closure['offline_link_and_sql_closure_passed'] and not closure['errors']
            and not closure['board_runtime_verified'], 'Offline closure did not pass')
    step('package', ['python3','-B',str(PROGRAM/'shell/package_arm64.py'),'package',
                     '--binary',str(BUILD/'sn100_shell'),'--sysroot',str(SYSROOT),
                     '--snapshot',str(FREEZE),'--dependencies',str(ARM/'dependencies/dependency_report.json'),
                     '--output',str(ARM/'packages')])
    package = audit_package(frozen)
    source_package = make_source_package(frozen, package['release_id'])
    step('snapshot_after_package', ['python3','-B',str(PROGRAM/'shell/package_arm64.py'),'snapshot',
                                    '--output',str(ARM/'source_after_package.json')])
    require(readjson(ARM/'source_after_package.json') == frozen, 'Sources changed during packaging')
    require(digest(FREEZE) == freeze_sha256, 'Native frozen snapshot file changed during ARM stages')
    sysroot_after = sysroot_inputs()
    save('sysroot_after.json', sysroot_after)
    require(sysroot_after == sysroot_before, 'Reused sysroot inputs changed')
    tool_after = {str(path): {'resolved':str(path.resolve(strict=True)), 'sha256':digest(path.resolve(strict=True))}
                  for path in tools}
    save('toolchain_after.json', tool_after)
    require(tool_after == tool_before, 'ARM compiler or Qt generator input changed during build')
    result = {'schema':'sn100-four-windows-arm64-build-offline-v1','finished_utc':now(),'source_entries':len(frozen),
              'source_before_after_and_package_equal_native_freeze':True,
              'build_exit_code':0,'closure_exit_code':0,'package_exit_code':0,
              'sysroot_reused_read_only':str(SYSROOT),'sysroot_fingerprint_unchanged':True,
              'full_object_recompilation':True,'object_summary':object_summary,
              'source_freeze_sha256':freeze_sha256,'toolchain_fingerprints_unchanged':True,
              'main_sha256':digest(BUILD/'sn100_shell'),'renderer_sha256':digest(BUILD/'sn100_report_renderer'),
              'closure_elf_count':len(closure['files']),'closure_errors':closure['errors'],
              'package':package,'source_package':source_package,'board_runtime_verified':False,'real_printer_or_instrument_used':False,
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
