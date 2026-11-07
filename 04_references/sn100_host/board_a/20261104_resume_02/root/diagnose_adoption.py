from pathlib import Path
import hashlib
import json
root = Path(__file__).resolve().parents[3]
folder = root / 'temp/sn100_host/resume_sample_20261104_02'
for name in ('cuscombox_base.cpp', 'cuscombox_base.h'):
    original = root / '06_programs/sn100_host/samples' / name
    for p in [original, *folder.rglob(name)]:
        b = p.read_bytes()
        print(json.dumps({'path': str(p.relative_to(root)), 'sha256': hashlib.sha256(b).hexdigest(), 'bytes': len(b), 'crlf': b.count(b'\r\n'), 'lf': b.count(b'\n')}))
