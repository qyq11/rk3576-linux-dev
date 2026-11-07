"""保全三份候选和实际编译证据；不采用到正式源或部署。"""
from pathlib import Path
import hashlib
import importlib.util
import json
import shutil
import tarfile

root = Path(__file__).resolve().parents[3]
work = Path(__file__).resolve().parent
archive = root / '07_backups/sn100_host/source_candidates/20261105_ui_drafts_ascii'
evidence = root / '04_references/sn100_host/source_preparation_20261105'
assert not archive.exists() and not evidence.exists()
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
before = json.loads((work / 'formal_before.json').read_text())
spec = importlib.util.spec_from_file_location('package_arm64', root / '06_programs/sn100_host/shell/package_arm64.py')
module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
after = {key.replace('\\', '/'): value for key, value in module.snapshot(root / '06_programs/sn100_host').items()}
assert before == after and len(after) == 697
(work / 'formal_after.json').write_text(json.dumps(after, indent=2) + '\n')
results = {}
all_files = {}
for kind in ('ascii', 'hospital', 'barcode'):
    frozen = json.loads((work / f'frozen_{kind}.json').read_text())
    compiled = json.loads((work / f'compiled/{kind}/result.json').read_text())
    assert compiled['kind'] == kind and not compiled['product_linked'] and not compiled['product_run'] and not compiled['board_accessed']
    for item in compiled['units']:
        assert item['exit'] == 0 and item['architecture'] == 'AArch64'
        assert sha(work / item['source']) == item['source_sha256']
        obj = work / 'compiled' / kind / (Path(item['source']).stem + '.o')
        assert sha(obj) == item['object_sha256']
    for rel, digest in frozen['after_sha256'].items():
        assert rel not in all_files and sha(work / 'compile_inputs' / kind / rel) == digest
        all_files[rel] = {'before': before[rel.removeprefix('06_programs/sn100_host/')], 'after': digest, 'kind': kind}
    results[kind] = {'units': len(compiled['units']), 'exit_codes': [item['exit'] for item in compiled['units']], 'patch_sha256': frozen['patch_sha256']}
assert len(all_files) == 5 and sum(item['units'] for item in results.values()) == 5
archive.mkdir(parents=True); evidence.mkdir(parents=True)
def copy(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    assert sha(source) == sha(destination)
locations = {
 'ascii': ('resume_ascii_input_20261105_01', 'soft_line_break.patch'),
 'hospital': ('resume_hospital_draft_20261105_01', 'hospital_remove_preserve_draft.patch'),
 'barcode': ('resume_barcode_draft_20261105_01', 'packaging_01/barcode_delete_draft.patch')}
for kind, (folder, patch) in locations.items():
    source = work.parent / folder
    copy(source / patch, archive / 'patches' / (kind + '.patch'))
    copy(source / 'handoff.md', archive / 'handoffs' / (kind + '.md'))
    copy(work / f'frozen_{kind}.json', archive / ('frozen_' + kind + '.json'))
    for p in source.iterdir():
        if p.is_file() and p.suffix in {'.json', '.md', '.sha256'}:
            copy(p, evidence / 'author_inputs' / kind / p.name)
    for p in (source / 'packaging_01').glob('*.json'):
        copy(p, evidence / 'author_inputs' / kind / 'packaging_01' / p.name)
for p in work.iterdir():
    if p.is_file() and p.suffix in {'.py', '.sh', '.md', '.json', '.txt', '.log', '.exit', '.stdout', '.stderr'}:
        copy(p, evidence / 'root' / p.name)
for kind in results:
    for p in (work / 'compiled' / kind).iterdir():
        if p.is_file() and p.suffix != '.o':
            copy(p, evidence / 'compiled' / kind / p.name)
sources_tar = archive / 'candidate_sources.tar.gz'
with tarfile.open(sources_tar, 'w:gz') as tar:
    for rel, item in sorted(all_files.items()):
        tar.add(work / 'compile_inputs' / item['kind'] / rel, arcname=rel, recursive=False)
with tarfile.open(sources_tar, 'r:gz') as tar:
    assert len(tar.getmembers()) == 5
    for member in tar:
        assert member.isfile() and hashlib.sha256(tar.extractfile(member).read()).hexdigest() == all_files[member.name]['after']
(archive / 'changed_files.json').write_text(json.dumps(all_files, indent=2) + '\n')
report = {'candidate_slices': 3, 'changed_source_files': 5, 'compiled_units': 5, 'compilation': results,
          'formal_sources_checked': 697, 'formal_source_differences': 0, 'source_tar_sha256': sha(sources_tar),
          'formal_adoption': False, 'full_product_build': False, 'product_run': False, 'board_accessed': False}
(archive / 'verification.json').write_text(json.dumps(report, indent=2) + '\n')
(archive / 'README.md').write_text('''# 医院删行、条码删除和通讯换行候选

2026-11-05，用户暂不方便配合板A实屏操作，要求先完成其他可推进部分。本目录保全三份独立修复候选，共5份源码；各片普通补丁check/apply和逐字节回放通过，3份CPP及2份Qt生成代码均已通过ARM64编译。正式697源保持11月4日87768ca4恢复包清单，**尚未采用或启用本目录代码**。

candidate_sources.tar.gz只存5份候选后像，patches保存普通窄补丁，changed_files.json逐文件列原SHA和后SHA，handoffs列原版实际入口、失败范围和RK验收步骤。恢复时先核正式前像，只在新的候选目录解压/应用；不要整树覆盖工作区或把本压缩包当可运行程序。头文件有改动，后续采用后需完整构建和RK验证，不能混用旧对象。

本轮没有完整程序链接、电脑产品运行、脱机/模拟、数据库执行、联板或仪器通过结论。原菜单实屏待验、医院新增后删新行的额外放弃提示、条码旧按ID/失败重读差异、NBSP/富文本转换等仍按交接保留。本目录不含现场配置、数据库、凭据或编译对象。

实际编译日志、参数和复核输入在[本轮证据](../../../../04_references/sn100_host/source_preparation_20261105/README.md)，唯一进度与后续在[同一记录](../../../../02_records/20261022_sn100_host_migration.md#prepare-without-manual-20261105)。
''', encoding='utf-8')
(evidence / 'README.md').write_text('''# 三份候选的源码与ARM64编译证据

本目录记录2026-11-05三窗口独立候选的源码核对、总控窄补丁重放、原版依据和ARM64编译结果。正式697源前后0差异；本轮没有运行产品、执行SQL、联板、登录或切版。11月4日板A结果保留当时范围，当前状态未复核。

compiled下为3份CPP和2份moc的实际命令、输出、退出码与对象指纹；仅编译成AArch64对象，没有完整程序链接。root下为生成头文件/取得正式编译参数的脚本、基线指纹和总控复核。author_inputs保留作者当时交接，其“未编译”不覆盖总控后来单独保存的编译结果。Qt源码缓存仍在temp，只保存来源URL和SHA等核对依据，不把它复制为产品源码。

候选压缩源码、三片补丁和交接在[正式候选保全](../../../07_backups/sn100_host/source_candidates/20261105_ui_drafts_ascii/README.md)。实际范围、原版剩余差异及下次步骤看[同一记录](../../../02_records/20261022_sn100_host_migration.md#prepare-without-manual-20261105)。这些工具带当次路径，只作证据，不直接重跑归档脚本。
''', encoding='utf-8')
counts = {}
for directory in (archive, evidence):
    entries = [p for p in sorted(directory.rglob('*')) if p.is_file() and p != directory / 'SHA256SUMS']
    manifest = ''.join(f'{sha(p)}  {p.relative_to(directory).as_posix()}\n' for p in entries)
    (directory / 'SHA256SUMS').write_text(manifest)
    for line in manifest.splitlines():
        digest, rel = line.split('  ', 1); assert sha(directory / rel) == digest
    counts[directory.name] = len(entries)
print(json.dumps({'report': report, 'manifest_entries': counts}, ensure_ascii=False, indent=2))
