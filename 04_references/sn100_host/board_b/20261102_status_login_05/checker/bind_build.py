"""确认05打包、冻结源码及全量对象相互匹配；不运行应用。"""
import hashlib
import json
import sys
from pathlib import Path

here = Path(__file__).resolve().parent
root = here.parents[4]
integration = root / 'temp/sn100_host/workflow_night_20261102/integration_05'
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
release_id = sys.argv[1]
prepared = json.loads((here / 'prepared_inputs.json').read_text())
snapshot = json.loads((here / 'source_snapshot_05.json').read_text())
if sha(here / 'status_login_review_05.cpp') != prepared['prepared_checker_sha256']:
    raise RuntimeError('候选检查器与已审准备输入不一致')
if json.loads((integration / 'source_snapshot.json').read_text()) != snapshot:
    raise RuntimeError('05构建输入清单已变化')
stage = Path((integration / 'packages/latest_release').read_text().strip())
if not stage.resolve().is_relative_to((integration / 'packages').resolve()):
    raise RuntimeError('05包目录越界')
if (stage / 'release_id').read_text().strip() != release_id or sha(stage / 'SHA256SUMS') != release_id:
    raise RuntimeError('发布编号不匹配')
if json.loads((stage / 'release.json').read_text())['sources'] != snapshot:
    raise RuntimeError('发布包不属于这份05源码')
if sha(stage / 'sn100_shell') != sha(integration / 'build/sn100_shell'):
    raise RuntimeError('05全量主程序与包不匹配')
for name, digest in snapshot.items():
    if sha(here / 'frozen_05/sn100_host' / name) != digest:
        raise RuntimeError('固定的05源码已变化：' + name)
rows = [line.split('\t', 1) for line in (integration / 'build_inputs.tsv').read_text().splitlines()]
source_root = Path(prepared['source_program'])
expected = sum(kind == 'source' for kind, _ in rows) + sum(kind == 'resource' for kind, _ in rows)
for kind, value in rows:
    if kind == 'header':
        name = Path(value).relative_to(source_root)
        expected += b'Q_OBJECT' in (here / 'frozen_05/sn100_host' / name).read_bytes()
objects = sorted((integration / 'arm64_build').glob('obj_*.o'), key=lambda p: int(p.stem[4:]))
if [p.name for p in objects] != ['obj_' + str(i) + '.o' for i in range(expected)]:
    raise RuntimeError('05全量对象数量或连续编号与输入不符')
hashes = {str(path): sha(path) for path in objects}
manifest_path = here / 'link_provenance.json'
record = {'release_id': release_id, 'application_sha256': sha(stage / 'sn100_shell'),
          'source_snapshot_sha256': sha(here / 'source_snapshot_05.json'),
          'checker_source_sha256': sha(here / 'status_login_review_05.cpp'),
          'object_count': len(objects), 'objects': hashes, 'main_object_kept': True}
if '--verify-objects' in sys.argv:
    if json.loads(manifest_path.read_text()) != record:
        raise RuntimeError('检查器链接期间05正式对象或绑定输入发生变化')
else:
    if manifest_path.exists():
        raise RuntimeError('本次链接证据已存在，请另建受审准备轮，不覆盖')
    manifest_path.write_text(json.dumps(record, indent=2) + '\n')
    (here / 'link_objects.txt').write_text(''.join(str(p) + '\n' for p in objects))
print('REVIEW_BOUND_TO_05=' + release_id + '; OBJECTS=' + str(len(objects)))
