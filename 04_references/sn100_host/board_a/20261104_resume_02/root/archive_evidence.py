"""保全本轮代码审查、构建与板A运行的有限证据，不复制依赖或业务数据。"""
from pathlib import Path
import hashlib
import json
import shutil

root = Path(__file__).resolve().parents[3]
work = Path(__file__).resolve().parent
evidence = root / '04_references/sn100_host/board_a/20261104_resume_02'
evidence.mkdir(parents=True, exist_ok=False)
allowed = {'.md', '.json', '.cpp', '.h', '.py', '.sh', '.patch', '.pro', '.ui', '.txt',
           '.sha256', '.stdout', '.stderr', '.exit', '.log', '.jsonl', '.tsv'}
def copy_file(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)
    assert hashlib.sha256(source.read_bytes()).digest() == hashlib.sha256(target.read_bytes()).digest()
for p in work.iterdir():
    if p.is_file() and not (p.name.startswith('live_collection') and p.suffix == '.stdout') and (p.suffix in allowed or p.name.endswith('SHA256SUMS')):
        copy_file(p, evidence / 'root' / p.name)
if (work / 'dependencies/dependency_report.json').exists():
    copy_file(work / 'dependencies/dependency_report.json', evidence / 'root/dependency_report.json')
for sub in ('collected', 'collected_final'):
    folder = work / sub
    if folder.is_dir():
        for p in folder.rglob('*'):
            if p.is_file(): copy_file(p, evidence / sub / p.relative_to(folder))
for folder in ('resume_ascii_20261104_02', 'resume_sample_20261104_02', 'resume_lis_B1_20261104_02'):
    source = work.parent / folder
    for p in source.rglob('*'):
        if not p.is_file() or any(part in {'.git', '__pycache__'} for part in p.parts): continue
        if p.suffix not in allowed and p.name != 'SHA256SUMS': continue
        copy_file(p, evidence / 'candidates' / folder / p.relative_to(source))
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
entries = [p for p in sorted(evidence.rglob('*')) if p.is_file()]
(evidence / 'files.json').write_text(json.dumps([{'path': p.relative_to(evidence).as_posix(), 'bytes': p.stat().st_size, 'sha256': sha(p)} for p in entries], indent=2) + '\n')
(evidence / 'README.md').write_text('# 板A恢复移植后的本轮证据\n\n2026-11-04，总控协调三个既有窗口，正式采用通讯时间颜色、容器焦点、LIS孔标志和只读映射三片6源。本目录保留候选窄补丁、前后指纹、原版只读依据、总控合入与构建回执，以及本轮取得的板A原始日志。没有数据库、现场配置或认证文件。\n\n编译、启动、通信显示、菜单交互及真实仪器分别登记；最终实际结果以[同一移植记录](../../../../02_records/20261022_sn100_host_migration.md#resume-20261104-02)为准，不能把脚本、候选或构建成功当作全部功能通过。root内的联板脚本带本轮固定状态，只作证据，不能直接重跑。匹配程序/697源保存在[板A恢复包](../../../../07_backups/sn100_host/board_a/resume_20261104_02/README.md)。\n\n源码、生成物、可变日志与中间脚本原工作位置在根temp/sn100_host；本目录是按SHA清单核对后的固定副本。只读观察器会增加少量采样工作，本次短时资源不构成门槛或长稳通过。\n', encoding='utf-8')
entries = [p for p in sorted(evidence.rglob('*')) if p.is_file() and p != evidence / 'SHA256SUMS']
(evidence / 'SHA256SUMS').write_text(''.join(f'{sha(p)}  {p.relative_to(evidence).as_posix()}\n' for p in entries))
for line in (evidence / 'SHA256SUMS').read_text().splitlines():
    digest, path = line.split('  ', 1)
    assert sha(evidence / path) == digest
print(json.dumps({'evidence': str(evidence), 'files': len(entries), 'bytes': sum(p.stat().st_size for p in entries)}))
