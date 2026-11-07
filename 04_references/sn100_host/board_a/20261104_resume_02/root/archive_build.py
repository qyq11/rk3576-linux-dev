"""把本轮匹配的程序包和697源保存到temp之外，并回读逐成员核对。"""
from pathlib import Path
import hashlib
import json
import shutil
import tarfile

root = Path(__file__).resolve().parents[3]
work = Path(__file__).resolve().parent
archive = root / '07_backups/sn100_host/board_a/resume_20261104_02'
archive.mkdir(parents=True, exist_ok=False)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
for name in ('build', 'dependencies', 'package'):
    assert (work / f'{name}.exit').read_text().strip() == '0'
mapping = json.loads((work / 'source_snapshot.json').read_text())
assert len(mapping) == 697
for rel, digest in mapping.items():
    assert sha(root / '06_programs/sn100_host' / rel) == digest
archive_package = (work / 'packages/latest_package').read_text().strip()
assert archive_package.startswith('/mnt/c/')
package = Path('C:/' + archive_package[len('/mnt/c/'):])
shutil.copy2(package, archive / package.name)
assert sha(package) == sha(archive / package.name)
shutil.copy2(work / 'source_snapshot.json', archive / 'source_snapshot.json')
source_tar = archive / 'source_files.tar.gz'
with tarfile.open(source_tar, 'w:gz') as tar:
    for rel in sorted(mapping):
        tar.add(root / '06_programs/sn100_host' / rel, arcname=rel, recursive=False)
with tarfile.open(source_tar, 'r:gz') as tar:
    assert len(tar.getmembers()) == 697
    for entry in tar:
        assert entry.isfile() and hashlib.sha256(tar.extractfile(entry).read()).hexdigest() == mapping[entry.name]
for rel in ('build.exit', 'dependencies.exit', 'package.exit', 'header_cache_verification.json', 'adoption_result.json'):
    shutil.copy2(work / rel, archive / rel)
report = {'sources_verified': 697, 'source_tar_sha256': sha(source_tar), 'package': package.name,
          'package_sha256': sha(archive / package.name), 'runtime_verification': '板A实际运行结果见同日移植记录与04固定证据，不由编译归档代替。'}
(archive / 'verification.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
(archive / 'README.md').write_text('# 板A本轮程序与匹配源码\n\n本目录保存2026-11-04恢复移植后新编译的程序包和697份对应源码，含通讯时间颜色、容器焦点及LIS孔标志/只读映射三片。用于恢复本次代码和程序，不含现场配置、业务数据库或认证材料。源码压缩包每个成员已与source_snapshot.json核对一致。\n\n编译、依赖和打包通过不代表按钮、完整LIS或仪器通过；实际范围见[同一记录](../../../../02_records/20261022_sn100_host_migration.md#resume-20261104-02)。板A的原current不因存包改变，板B不采用本包。恢复时重新核板A公钥/序列和实际环境，不重跑历史固定进程脚本。\n', encoding='utf-8')
(archive / 'SHA256SUMS').write_text(''.join(f'{sha(p)}  {p.name}\n' for p in sorted(archive.iterdir()) if p.is_file() and p.name != 'SHA256SUMS'))
print(json.dumps(report, ensure_ascii=False))
