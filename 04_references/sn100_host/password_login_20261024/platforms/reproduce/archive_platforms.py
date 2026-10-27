#!/usr/bin/env python3
"""Publish audited platform metadata and one immutable ARM restore package."""
import hashlib
import importlib.util
import json
import re
import shutil
import subprocess
import tarfile
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[4]
PROGRAM = ROOT / '06_programs/sn100_host'
WORK = ROOT / 'temp/sn100_host/o02_login/platforms'
AUTH = WORK.parent
DEST = ROOT / '04_references/sn100_host/password_login_20261024/platforms'
BACKUP = ROOT / '07_backups/sn100_host/password_login_20261024'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')


def copy(source, relative):
    target = DEST / relative
    assert source.is_file() and not target.exists(), str(target)
    assert source.suffix.lower() not in {'.db', '.secret', '.exe', '.o'}
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)


assert not DEST.exists() and not BACKUP.exists()
spec = importlib.util.spec_from_file_location('package', PROGRAM / 'shell/package_arm64.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)
current = package.snapshot(PROGRAM)
native = read(WORK / 'native_audit/verification.json')
windows = read(WORK / 'windows/verification.json')
arm = read(WORK / 'arm64/verification.json')
assert len(current) == 353
for path in [AUTH / 'native/before.json', AUTH / 'native/after.json',
             WORK / 'windows/source_before.json', WORK / 'windows/source_after.json',
             WORK / 'arm64/source_before.json', WORK / 'arm64/source_after.json']:
    assert read(path) == current, str(path)
assert native['make_q_all_140_objects_exit'] == 0 and native['all_changed_dependency_objects_actually_compiled']
assert windows['published'] and windows['exit_code'] == 0 and windows['reused_objects'] == 0
assert len(windows['stages']) == 2 and all(stage['make_exit'] == 0 for stage in windows['stages'])
assert arm['sources_tools_unchanged'] and arm['source_matches_native_freeze'] and not arm['closure_errors']
assert arm['reused_objects'] == 0 and arm['all_object_elf_dwarf_source_verified']
static_log = (AUTH / 'logs/arm64_inputs.log').read_text()
assert re.search(r'^Ran 11 tests in [\d.]+s$', static_log, re.MULTILINE)
assert static_log.strip().endswith('OK') and not re.search(r'FAILED|ERROR|FAIL:', static_log)
warning_sources = ['instrument/frmdriverset.ui', 'thirdparty/qtrpt/QtRPT/qtrpt.cpp',
                   'samples/material/qtmaterialscrollbar.cpp']
old_sources = read(ROOT / 'temp/sn100_host/o09_loopback/native/interim_after.json')
assert all(current[name] == old_sources[name] for name in warning_sources)
windows_warnings = [line for name in ('shell_make.log', 'renderer_make.log')
                    for line in (WORK / 'windows' / name).read_text(encoding='utf-8-sig').splitlines()
                    if 'warning:' in line.lower()]

arm_dir = WORK / 'arm64'
stage = Path((arm_dir / 'packages/latest_release').read_text().strip())
release = read(stage / 'release.json')
release_id = (stage / 'release_id').read_text().strip()
assert release['sources'] == current and 'local_password_login' in release['capabilities']
assert release['dependencies_sha256'] == sha(arm_dir / 'dependencies/dependency_report.json')
assert sha(stage / 'SHA256SUMS') == release_id
package_path = Path((arm_dir / 'packages/latest_package').read_text().strip())
assert sha(package_path) == arm['package_sha256']
with tarfile.open(package_path, 'r:gz') as bundle:
    members = bundle.getmembers()
    names = {member.name for member in members}
    assert len(names) == len(members)
    for member in members:
        normalized = PurePosixPath(member.name)
        assert not normalized.is_absolute() and '..' not in normalized.parts
        assert not member.issym() and not member.islnk()
        assert normalized.suffix.lower() not in {'.db', '.secret', '.log'}
    sums = bundle.extractfile('SHA256SUMS').read()
    assert hashlib.sha256(sums).hexdigest() == release_id
    for line in sums.decode('ascii').splitlines():
        digest, name = line.split('  ', 1)
        assert hashlib.sha256(bundle.extractfile(name).read()).hexdigest() == digest, name
    assert bundle.extractfile('release_id').read().decode().strip() == release_id

BACKUP.mkdir(parents=True)
backup_package = BACKUP / package_path.name
shutil.copyfile(package_path, backup_package)
assert sha(backup_package) == arm['package_sha256']
source_manifest = BACKUP / 'source_snapshot.json'
shutil.copyfile(AUTH / 'native/after.json', source_manifest)
assert read(source_manifest) == current
source_package = BACKUP / f'sn100_source_{release_id}.tar.gz'
for name, digest in current.items():
    normalized = PurePosixPath(name)
    source = PROGRAM / name
    assert not normalized.is_absolute() and '..' not in normalized.parts and '\\' not in name
    assert source.is_file() and not source.is_symlink() and sha(source) == digest
    assert normalized.suffix.lower() not in {'.db', '.sqlite', '.sqlite3', '.ini', '.secret', '.exe',
        '.o', '.obj', '.a', '.so', '.lib', '.dll', '.dylib', '.key', '.pem'}
    assert not re.search(r'\.so(?:\.[0-9]+)*$', normalized.name)
    with source.open('rb') as stream:
        prefix = stream.read(32)
    assert not any(prefix.startswith(magic) for magic in (b'MZ', b'\x7fELF', b'!<arch>\n', b'SQLite format 3\0'))
with tarfile.open(source_package, 'w:gz') as bundle:
    for name in sorted(current):
        bundle.add(PROGRAM / name, arcname=name, recursive=False)
with tarfile.open(source_package, 'r:gz') as bundle:
    members = bundle.getmembers()
    assert len(members) == 353 and {member.name for member in members} == set(current)
    for member in members:
        assert member.isfile() and not member.issym() and not member.islnk()
        assert hashlib.sha256(bundle.extractfile(member).read()).hexdigest() == current[member.name]
source_restore = {'path': source_package.relative_to(ROOT).as_posix(), 'sha256': sha(source_package),
                  'member_count': 353, 'manifest_path': source_manifest.relative_to(ROOT).as_posix(),
                  'manifest_sha256': sha(source_manifest)}
save(BACKUP / 'origins.json', {'source': str(package_path.relative_to(ROOT)), 'sha256': sha(backup_package),
     'release_id': release_id, 'source_entries': 353, 'capability': 'local_password_login',
     'restore_scope': 'User-level ARM main/helper, private required libraries/plugins/fonts/licenses and embedded resources; no runtime database or credentials',
     'source_restore_archive': source_restore,
     'deployed': False, 'board_runtime_verified': False})
(BACKUP / 'README.md').write_text(
    '# SN100 本地密码登录 ARM64 离线恢复包\n\n'
    f'ARM64发布包来自当前353项冻结源码的全量主辅构建，release ID为`{release_id}`。'
    '平台证据见[统一平台索引](../../../04_references/sn100_host/password_login_20261024/platforms/verification.json)。'
    '先执行`sha256sum -c SHA256SUMS`，解包到新目录后再核包内SHA256SUMS和release_id。\n\n'
    '恢复范围为用户级统一主程序、报告辅助程序、必要私有Qt SQL/SQLite/报表/SVG库及插件、字体/许可和嵌入模板资源。'
    '不含系统镜像、内核、全机配置、运行数据库、密码或其他凭据；不迁移或重置已有数据。默认Guest，本地密码登录仅限显式隔离测试库；'
    '本机回环须另显式启用，登录不改变设备动作条件。\n\n'
    '本轮未上传、安装、启动RK或连接仪器。恢复部署前仍须核目标动态库、显示/输入、权限和原生命周期合同；'
    '离线依赖闭包与编译通过不代替RK实际运行、原Windows行为、真实仪器或长期稳定性验收。旧发布包保持原位置。\n\n'
    f'另附`{source_package.name}`和`source_snapshot.json`，仅保存本版本明确的353项构建输入，成员路径相对`06_programs/sn100_host/`。'
    '源码包已逐成员读回核SHA，按清单恢复到新的program目录后再构建；它不含完整Git历史、根级文档、设备配置或运行数据。'
    'Qt/工具链等外部输入继续使用既有07_backups/qt_environment归档，不将本源码包当系统备份。两个tar.gz均沿既有Git LFS规则保存。\n', encoding='utf-8')
(BACKUP / 'SHA256SUMS').write_text(''.join(f'{sha(path)}  {path.name}\n' for path in sorted(BACKUP.iterdir())
                                        if path.is_file()), encoding='ascii')
attribute = subprocess.check_output(['git', 'check-attr', 'filter', 'diff', 'merge', '--',
    str(backup_package.relative_to(ROOT)), str(source_package.relative_to(ROOT))], cwd=ROOT, text=True)
assert len(attribute.splitlines()) == 6 and all(line.endswith(': lfs') for line in attribute.splitlines())

for directory, target in [('windows', 'windows'), ('native_audit', 'native/audit')]:
    for source in sorted((WORK / directory).glob('*')):
        if source.is_file() and source.suffix in {'.json', '.log'}:
            copy(source, Path(target) / source.name)
for name in ('before.json', 'after.json', 'cache_provenance.json'):
    copy(AUTH / 'native' / name, Path('native') / name)
copy(AUTH / 'logs/native.log', Path('native/build.log'))
copy(AUTH / 'logs/arm64_inputs.log', Path('static/arm64_inputs.log'))
for name in ('verification.json', 'source_before.json', 'source_after.json', 'prepare.log', 'final_check.log',
             'actual_input_list.tsv', 'effective_input_list.tsv', 'tool_versions.log', 'dwarf_object_sources.json',
             'trace_diagnostic.json', 'parser_failure_note.json', 'postprocess_corrected.log',
             'postprocess_second_attempt_failed.log', 'verification_second_attempt_unaccepted.json',
             'postprocess_third_attempt_pre_atomic.log', 'verification_third_attempt_pre_atomic.json'):
    copy(arm_dir / name, Path('arm64') / name)
for directory in ('dependencies', 'final_dependencies'):
    for source in sorted((arm_dir / directory).iterdir()):
        if source.is_file():
            copy(source, Path('arm64') / directory / source.name)
copy(stage / 'release.json', Path('arm64/release.json'))
copy(stage / 'SHA256SUMS', Path('arm64/package_contents.SHA256SUMS'))
copy(stage / 'release_id', Path('arm64/release_id'))
save(DEST / 'build_warning_notes.json', {
    'windows_warning_lines': windows_warnings,
    'windows_warning_count': len(windows_warnings),
    'classification': '17 original instrument uic duplicate layout names; four QtRPT unused band variables across main/helper; one original Material scrollbar unused Q_D',
    'warning_source_sha_matches_previous_checkpoint': {name: current[name] for name in warning_sources},
    'changed_to_hide_warnings': False,
    'native_clock_skew': 'Retained in original log; explicit140-object make-q=0, changed dependency real compiler commands and reused SHA/flags audited separately',
})
for name in ('prepare_inventory.json', 'prepare_arm_inputs.tsv', 'arm-before_inventory.json',
             'arm-before_arm_inputs.tsv', 'arm-after_inventory.json', 'arm-after_arm_inputs.tsv'):
    copy(WORK / name, Path('provenance') / name)
for name in ('build_windows.ps1', 'file_helpers.ps1', 'makefile_audit.ps1', 'build_arm64.sh',
             'prepare_inventory.py', 'prepare_inventory_first_pass.py', 'prepare_inventory_second_pass.py',
             'diagnose_trace.py', 'verify_arm_dwarf.py',
             'audit_native.py', 'audit_native_flags.py', 'archive_platforms.py'):
    copy(WORK / name, Path('reproduce') / name)
(DEST / 'git_lfs_attributes.txt').write_text(attribute)

artifacts = []
for kind, path in [
    ('native_main', AUTH / 'native/wsl_release/sn100_shell'),
    ('native_renderer', AUTH / 'native/wsl_release/sn100_report_renderer'),
    ('windows_main', WORK / 'windows/final_release/sn100_shell.exe'),
    ('windows_renderer', WORK / 'windows/final_release/sn100_report_renderer.exe'),
    ('arm64_main', arm_dir / 'build/sn100_shell'),
    ('arm64_renderer', arm_dir / 'build/sn100_report_renderer'),
    ('arm64_package_backup', backup_package),
]:
    artifacts.append({'kind': kind, 'path': path.relative_to(ROOT).as_posix(), 'sha256': sha(path)})
save(DEST / 'verification.json', {
    'schema': 'sn100-password-login-platforms-v1',
    'current_source_entries': len(current), 'current_source_matches_native': True,
    'build_exit_codes': {'native': 0, 'windows_main': windows['stages'][0]['make_exit'],
                         'windows_renderer': windows['stages'][1]['make_exit'], 'arm64_prepare': 0},
    'safe_artifacts': artifacts,
    'formal_package': {'release_id': release_id, 'password_capability_key': 'local_password_login',
                       'backup_path': backup_package.relative_to(ROOT).as_posix(), 'sha256': sha(backup_package),
                       'source_restore_archive': source_restore},
    'native_audit': {'actual_compile_commands': native['compiled_command_count'],
                     'account_header_dependency_objects_recompiled': native['account_header_dependency_objects'],
                     'all_changed_dependency_objects_compiled': True, 'all_140_objects_make_q_exit': 0,
                     'reused_unchanged_objects': native['reused_object_count'],
                     'default_make_q_exit': 1, 'default_make_q_reason': native['default_make_q_explanation'],
                     'original_clock_skew_warning_preserved': True},
    'windows': {'architecture': 'PE32/i386', 'main_full_compiled_objects': windows['stages'][0]['compiled_objects'],
                'renderer_full_compiled_objects': windows['stages'][1]['compiled_objects'], 'reused_objects': 0,
                'existing_warning_count': len(windows_warnings), 'warning_notes': 'build_warning_notes.json'},
    'arm64': {'architecture': 'AArch64', 'full_compiled_objects': arm['object_count'], 'reused_objects': 0,
              'dependency_file_count': arm['dependency_file_count'], 'closure_errors': [],
              'object_source_proof': 'Independent readelf DWARF pairing of existing142 objects with frozen75+57+8 sequence and2 helper sources',
              'original_parallel_xtrace_single_line_complete': False,
              'prepare_exit_code': 0, 'final_closure_exit_code': 0, 'first_wrapper_postprocessor_exit_code': 1,
              'corrected_read_only_postprocessor_exit_code': 0, 'failure_note': 'arm64/parser_failure_note.json',
              'postprocessor_attempt_exit_codes': [1, 1, 0, 0], 'success_json_published_atomically': True,
              'input_inventory_scope': 'Early actual_input_list.tsv enumerates the correct source/form/header/resource order with default include roots; effective_input_list.tsv uses the actual private sysroot/build roots. Executed flags are in original trace/DWARF.',
              'archive_all_member_sha256_checked': True},
    'static_arm_input_tests': {'tests': 11, 'failed': 0, 'exit_code': 0, 'log': 'static/arm64_inputs.log'},
    'limits': {'windows_gui_verified': False, 'rk_execution_verified': False, 'instrument_verified': False,
               'production_credential_security_verified': False, 'deployment_performed': False},
    'sensitive_data_archived': False,
    'scope': 'Build, source/object/flags and offline dependency/package provenance; business tests/resources are indexed separately by root',
})
(DEST / 'SOURCE_SHA256SUMS').write_text(''.join(f'{digest}  {name}\n' for name, digest in sorted(current.items())))
manifest = ''.join(f'{sha(path)}  {path.relative_to(DEST).as_posix()}\n' for path in sorted(DEST.rglob('*')) if path.is_file())
(DEST / 'SHA256SUMS').write_text(manifest)
print(json.dumps({'index': str(DEST / 'verification.json'), 'evidence_files_hashed': len(manifest.splitlines()),
                  'manifest_sha256': sha(DEST / 'SHA256SUMS'), 'backup': str(backup_package),
                  'package_sha256': sha(backup_package), 'release_id': release_id}))
