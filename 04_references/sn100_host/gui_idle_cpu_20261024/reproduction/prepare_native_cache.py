import hashlib
import json
from pathlib import Path
import shutil

root = Path('/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux')
old = root / 'temp/sn100_host/status_replay_20261024/native'
new = root / 'temp/sn100_host/o09_loopback/native'
assert not new.exists()
new.mkdir()
for name in ('wsl_release', 'wsl_renderer_release'):
    shutil.copytree(old / name, new / name, copy_function=shutil.copy2)
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
app = new / 'wsl_release/sn100_shell'
assert sha(app) == '150c9f0335e564cc0cef367c82a876908afa1177657e3944864e01d12fd54bbc'
(new / 'cache_provenance.json').write_text(json.dumps({'source': str(old),
    'copy': str(new), 'main_before_sha256': sha(app),
    'helper_before_sha256': sha(new / 'wsl_release/sn100_report_renderer'),
    'scope': 'Separate same-architecture build cache; qmake regenerated in destination before use; old build retained'}, indent=2))
print('NATIVE_CACHE_COPIED')
