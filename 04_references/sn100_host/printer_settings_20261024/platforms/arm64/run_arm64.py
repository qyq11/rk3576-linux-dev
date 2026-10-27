#!/usr/bin/env python3
"""O10-1 isolated full ARM64 build and static packaging; run only after root go."""
import datetime
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import time

ROOT = Path('/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux')
PROGRAM = ROOT / '06_programs/sn100_host'
WORK = ROOT / 'temp/sn100_host/printer_settings_20261024'
ARM = WORK / 'platforms/arm64'
BUILD = ARM / 'build'
SYSROOT = ROOT / 'temp/sn100_host/o02_login/platforms/arm64/sysroot'
FREEZE = WORK / 'native/final_before.json'
STEPS = []
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
    with (ARM / (name + '.log')).open('wb') as stream:
        result = subprocess.run(record['argv'], cwd=ROOT, env=env, stdout=stream,
                                stderr=subprocess.STDOUT, check=False, pass_fds=pass_fds)
    record.update(exit_code=result.returncode, finished_utc=now())
    STEPS.append(record)
    save(name + '_exit.json', record)
    require(result.returncode == 0, name + ' failed with exit ' + str(result.returncode))
def sysroot_inputs():
    original = readjson(ROOT / '04_references/sn100_host/password_login_20261024/platforms/arm64/final_dependencies/dependency_report.json')
    require(original['offline_link_and_sql_closure_passed'] and not original['errors'],
            'Old sysroot must have a successful recorded closure')
    paths = set()
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
def audit_objects(start_ns):
    rows = [line.split('\t', 1) for line in (ARM / 'actual_input_list.tsv').read_text().splitlines()]
    formal = [Path(value) for role, value in rows if role == 'source']
    headers = [Path(value) for role, value in rows if role == 'header']
    generated_moc = [BUILD / ('moc_' + p.stem + '.cpp') for p in headers if b'Q_OBJECT' in p.read_bytes()]
    generated_qrc = [BUILD / ('qrc_' + n + '.cpp') for n in [
        'shell_resources', 'account_resources', 'report_resources', 'imagesRpt',
        'projects_resources', 'reagents_resources', 'samples_resources', 'instrument_resources']]
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
                        'elf': 'ELF64 REL AArch64', 'dwarf': attributes})
    report = {'formal_units': len(formal), 'generated_moc_units': len(generated_moc),
              'generated_qrc_units': len(generated_qrc), 'main_objects': len(ordered),
              'helper_exclusive_objects': len(exclusive_helpers), 'total_objects': len(expected),
              'all_sources_and_producer_flags_verified': True, 'objects': records}
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
    require('local_printer_settings' in release['capabilities']
            and '--printer-settings' in release['startup_feature_arguments'], 'Printer feature absent from package')
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
def main():
    require(sys.argv[1:] == ['--root-go'], 'Explicit root go argument required')
    require(os.geteuid() != 0, 'Run as ordinary WSL user qyq')
    require(FREEZE.is_file(), 'Completed native freeze required')
    require(not BUILD.exists() and not (ARM / 'verification.json').exists()
            and not (ARM / 'steps.json').exists(), 'Fresh build/evidence required; preserve existing failure')
    frozen = readjson(FREEZE)
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
    trace_source.write_text('if [[ $0 == */sn100_host/shell/build.sh ]]; then PS4="+ "; set -x; fi\n', encoding='utf-8')
    start_ns = time.time_ns()
    with (ARM / 'actual_commands.log').open('wb') as trace:
        build_env = dict(env, BASH_ENV=str(trace_source), BASH_XTRACEFD=str(trace.fileno()))
        step('build', ['bash', str(PROGRAM / 'shell/build.sh'), str(BUILD / 'sn100_shell')],
             env=build_env, pass_fds=(trace.fileno(),))
    step('snapshot_after_build', ['python3','-B',str(PROGRAM/'shell/package_arm64.py'),'snapshot',
                                  '--output',str(ARM/'source_after_build.json')])
    require(readjson(ARM/'source_after_build.json') == frozen, 'Sources changed during full ARM build')
    object_summary = audit_objects(start_ns)
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
    step('snapshot_after_package', ['python3','-B',str(PROGRAM/'shell/package_arm64.py'),'snapshot',
                                    '--output',str(ARM/'source_after_package.json')])
    require(readjson(ARM/'source_after_package.json') == frozen, 'Sources changed during packaging')
    sysroot_after = sysroot_inputs()
    save('sysroot_after.json', sysroot_after)
    require(sysroot_after == sysroot_before, 'Reused sysroot inputs changed')
    result = {'schema':'sn100-printer-arm64-build-offline-v1','finished_utc':now(),'source_entries':len(frozen),
              'source_before_after_and_package_equal_native_freeze':True,
              'build_exit_code':0,'closure_exit_code':0,'package_exit_code':0,
              'sysroot_reused_read_only':str(SYSROOT),'sysroot_fingerprint_unchanged':True,
              'full_object_recompilation':True,'object_summary':object_summary,
              'main_sha256':digest(BUILD/'sn100_shell'),'renderer_sha256':digest(BUILD/'sn100_report_renderer'),
              'closure_elf_count':len(closure['files']),'closure_errors':closure['errors'],
              'package':package,'board_runtime_verified':False,'real_printer_or_instrument_used':False,
              'formal_source_or_shared_document_written':False,'old353_evidence_modified':False}
    save('steps.json', STEPS)
    save('verification.json', result)
    print(json.dumps(result, ensure_ascii=False))
if __name__ == '__main__':
    try:
        main()
    except BaseException as error:
        save('steps.json', STEPS)
        save('failure.json', {'failed_utc':now(),'error':str(error),'error_type':type(error).__name__,
                              'completed_steps':STEPS,'no_previous_outputs_removed_or_overwritten':True})
        raise
