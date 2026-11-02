"""只核文件、SHA和交付入口，不执行任何产品或RK功能。"""
import hashlib
import json
import pathlib
import tarfile

work = pathlib.Path(__file__).resolve().parent
root = work.parents[2]
evidence = root / '04_references/sn100_host/resume_20261030'
backup = root / '07_backups/sn100_host/resume_20261030'
for directory in (evidence, backup):
    files = sorted(p for p in directory.rglob('*') if p.is_file() and p.name != 'SHA256SUMS')
    lines = [hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + p.relative_to(directory).as_posix() for p in files]
    (directory / 'SHA256SUMS').write_text('\n'.join(lines) + '\n')
    for line in lines:
        expected, name = line.split('  ', 1)
        assert hashlib.sha256((directory / name).read_bytes()).hexdigest() == expected, name
    print('VERIFIED_ARCHIVE_FILES=' + str(len(files)) + ' ' + str(directory), flush=True)
identity = json.loads((backup / 'source_bundle_identity.json').read_text())
with tarfile.open(backup / 'sn100_resume_sources.tar.gz') as archive:
    for name, digest in identity.items():
        assert hashlib.sha256(archive.extractfile('06_programs/sn100_host/' + name).read()).hexdigest() == digest, name
print('VERIFIED_SOURCE_BUNDLE_FILES=' + str(len(identity)), flush=True)
for name in ('README.md', '01_docs/plans/sn100_host_migration_plan.md', '01_docs/plans/next_steps.md',
             '06_programs/sn100_host/README.md', '02_records/20261022_sn100_host_migration.md',
             '04_references/sn100_host/resume_20261030/README.md', '07_backups/sn100_host/resume_20261030/README.md'):
    assert (root / name).is_file(), name
plan = (root / '01_docs/plans/sn100_host_migration_plan.md').read_text(encoding='utf-8')
for title in ('## 一、已完成', '## 二、当前需要做', '## 三、未完成即将执行', '## 四、将来需要做'):
    assert title in plan, title
record = (root / '02_records/20261022_sn100_host_migration.md').read_text(encoding='utf-8')
assert 'id="quality-reagents-resume-20261030"' in record
print('DOCUMENT_ENTRY_AND_FOUR_PLAN_SECTIONS_OK=1', flush=True)
