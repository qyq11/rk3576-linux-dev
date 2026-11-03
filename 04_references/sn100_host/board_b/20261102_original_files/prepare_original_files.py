"""核对原资料并准备板B工作副本；只输出非敏感参数及文件清单。"""
from pathlib import Path, PurePosixPath
import hashlib
import json
import shutil
import tarfile

root = Path(__file__).resolve().parents[4]
out = Path(__file__).resolve().parent
source = root / "06_programs/sn100_host_reference/Seerain450_PC/bin"
theme = Path('/mnt/c/Users/52400/xwechat_files/wxid_7yopmftvc95322_6830/msg/file/2026-11/theme_config.ini')
manifest_path = root / "04_references/sn100_host/original_db_20261027/original_manifest.json"
manifest = json.loads(manifest_path.read_text(encoding='utf-8-sig'))
payload = out / 'working'
assert not payload.exists(), '工作副本已存在，先核对后继续，不能覆盖。'
payload.mkdir(parents=True)
sha = lambda b: hashlib.sha256(b).hexdigest()
assert len(manifest['files']) == 141
for entry in manifest['files']:
    rel = PurePosixPath(entry['path'])
    assert not rel.is_absolute() and '..' not in rel.parts
    src = source.joinpath(*rel.parts)
    assert src.is_file() and not src.is_symlink()
    data = src.read_bytes()
    assert sha(data) == entry['sha256'] and len(data) == entry['bytes'], f'原文件内容有变化，需核对：{rel}'
    if rel.suffix.lower() == '.db':
        assert all(not Path(str(src) + suffix).exists() for suffix in ['-wal', '-shm', '-journal'])
    dst = payload.joinpath(*rel.parts)
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(data)

def ini_pairs(path):
    section = ''
    values = {}
    for line in path.read_text(encoding='utf-8-sig').splitlines():
        s = line.strip()
        if not s or s.startswith(('#', ';')):
            continue
        if s.startswith('[') and s.endswith(']'):
            section = s[1:-1]
        elif '=' in line:
            key, value = line.split('=', 1)
            values[section + '/' + key.strip()] = value.strip()
    return values

old_theme = ini_pairs(payload / 'theme_config.ini')
new_theme = ini_pairs(theme)
changes = []
for key in sorted(old_theme.keys() | new_theme.keys()):
    if old_theme.get(key) != new_theme.get(key):
        # 主题显示/机型参数可留证，不输出医院和认证配置。
        assert key.startswith('theme/'), f'附件还有其他设置变更，需核对：{key}'
        changes.append({'file': 'theme_config.ini', 'key': key, 'before': old_theme.get(key), 'after': new_theme.get(key), 'basis': '用户2026-11-02在主控资料聊天提供的INI与截图'})
(payload / 'theme_config.ini').write_bytes(theme.read_bytes())
files = []
for path in sorted(payload.rglob('*')):
    if path.is_file():
        data = path.read_bytes()
        files.append({'path': path.relative_to(payload).as_posix(), 'bytes': len(data), 'sha256': sha(data)})
summary = {'board': 'board_b', 'serial': '8314b1e8ffd0321f', 'sourceManifest': str(manifest_path.relative_to(root)), 'files': files, 'fileCount': len(files), 'databaseCount': sum(f['path'].endswith('.db') for f in files), 'configurationChanges': changes, 'themeSourceSHA256': sha(theme.read_bytes())}
(out / 'prepared_manifest.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
(out / 'FILES.sha256').write_text(''.join(f"{f['sha256']}  {f['path']}\n" for f in files), encoding='utf-8')
print(json.dumps({'files': len(files), 'databases': summary['databaseCount'], 'themeChanges': changes, 'connectionKeys': {k: v for k, v in ini_pairs(payload / 'config.ini').items() if k.startswith('tcpmain/') and any(x in k.lower() for x in ['ip', 'port', 'type'])}, 'debugLoginKeys': {k: v for k, v in ini_pairs(payload / 'config_Debug.ini').items() if k.startswith('login/')}}, ensure_ascii=False, indent=2))
