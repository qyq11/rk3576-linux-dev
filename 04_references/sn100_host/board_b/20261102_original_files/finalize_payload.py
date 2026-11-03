"""只调整获准的同板连接地址，保留其他配置原文并登记逐键变化。"""
from pathlib import Path
import hashlib
import json
import re
import tarfile

out = Path(__file__).resolve().parent
payload = out / 'working'
summary = json.loads((out / 'prepared_manifest.json').read_text(encoding='utf-8'))
path = payload / 'config.ini'
raw = path.read_bytes()
text = raw.decode('utf-8-sig')
section = ''
seen = set()
lines = []
for line in text.splitlines(keepends=True):
    clean = line.strip()
    if clean.startswith('[') and clean.endswith(']'):
        section = clean[1:-1]
    match = re.match(r'^(\s*ip\s*=\s*)([^\r\n]*)(\r?\n)?$', line)
    if section in ('tcpmain', 'tcplog') and match:
        key = section + '/ip'
        assert key not in seen
        seen.add(key)
        before = match.group(2).strip()
        line = match.group(1) + '127.0.0.1' + (match.group(3) or '')
        if before != '127.0.0.1':
            summary['configurationChanges'].append({'file': 'config.ini', 'key': key, 'before': before, 'after': '127.0.0.1', 'basis': '用户确认连接板B本机SN500主控；与主控连接聊天的交接一致'})
    lines.append(line)
assert seen == {'tcpmain/ip', 'tcplog/ip'}
changed = ''.join(lines).encode('utf-8')
if raw.startswith(b'\xef\xbb\xbf'):
    changed = b'\xef\xbb\xbf' + changed
path.write_bytes(changed)
sha = lambda b: hashlib.sha256(b).hexdigest()
for item in summary['files']:
    data = (payload / item['path']).read_bytes()
    item.update(bytes=len(data), sha256=sha(data))
summary['scope'] = '原141份资料，29份原库结构与原值保留；以新SN500主题和获准同板地址更新两份INI；不用匿名测试资料'
(out / 'effective_manifest.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
(out / 'configuration_changes.json').write_text(json.dumps(summary['configurationChanges'], ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
(out / 'FILES.sha256').write_text(''.join(f"{f['sha256']}  {f['path']}\n" for f in summary['files']), encoding='utf-8')
archive = out / 'board_b_original_files.tar.gz'
with tarfile.open(archive, 'w:gz') as tar:
    for item in summary['files']:
        tar.add(payload / item['path'], arcname=item['path'], recursive=False)
summary_archive = {'file': archive.name, 'sha256': sha(archive.read_bytes()), 'bytes': archive.stat().st_size, 'files': len(summary['files']), 'databases': summary['databaseCount']}
(out / 'package_identity.json').write_text(json.dumps(summary_archive, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'package': summary_archive, 'configurationChanges': summary['configurationChanges']}, ensure_ascii=False, indent=2))
