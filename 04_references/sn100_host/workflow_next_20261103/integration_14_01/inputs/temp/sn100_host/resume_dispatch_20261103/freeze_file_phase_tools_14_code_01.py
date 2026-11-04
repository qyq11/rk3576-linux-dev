"""Root按独审清单复制九代码；只冻结代码，不创建执行配置或操作板卡。"""
from pathlib import Path
import hashlib
import json

ROOT = Path(__file__).resolve().parents[3]
TASK = ROOT / 'temp/sn100_host/workflow_next_20261103'
HERE = Path(__file__).resolve().parent
SOURCE = TASK / 'build_archive_candidate_14_01'
OLD = TASK / 'integration_13_root_frozen_01'
OUT = TASK / 'integration_14_root_frozen_01'
REVIEW = HERE / 'build_archive_14_tool_delta_review_01.md'
LIST = HERE / 'build_archive_14_tool_delta_review_01.tools.json'


def record(path):
    raw = path.read_bytes()
    return {'path': path.relative_to(ROOT).as_posix(), 'sha256': hashlib.sha256(raw).hexdigest(), 'bytes': len(raw)}


def save(path, raw):
    with path.open('xb') as stream:
        stream.write(raw)


def main():
    manifest_path = SOURCE / 'candidate_manifest.json'
    assert record(manifest_path)['sha256'] == 'd10e90e2c7c6c3bcb4babc8f422f8564591f805a1529c448c35e7a5d5697dd72'
    assert record(REVIEW)['sha256'] == '862df85ab9c6e4a1bc212152fc94f3ef566260a19fd89370b4e1248baf26ad24'
    assert record(LIST)['sha256'] == '5b34d3dbd995d74159709209b5c43893a850c75239cc72a18b2f8063cb28e920'
    manifest = json.loads(manifest_path.read_bytes())
    approved = json.loads(LIST.read_bytes())
    assert manifest['code_locked'] is True and len(approved) == len(manifest['code_tools']) == 9
    assert sorted(approved, key=lambda x: x['path']) == sorted(manifest['code_tools'], key=lambda x: x['path'])
    assert record(OLD / 'root_code_snapshot_13_01.json') == manifest['source_root_snapshot']
    seen, blobs = set(), {}
    for item in approved:
        path = ROOT / item['path']
        assert path.parent == SOURCE and path.name not in seen and record(path) == item
        seen.add(path.name)
        raw = path.read_bytes()
        old = OLD / path.name.replace('14_01', '13_01')
        assert raw == old.read_bytes().replace(b'13_01', b'14_01').replace(b'13-01', b'14-01')
        blobs[path.name] = raw
    assert not OUT.exists() and not OUT.is_symlink() and not (TASK / 'integration_14_01').exists()
    OUT.mkdir()
    for name in sorted(blobs):
        save(OUT / name, blobs[name])
        assert (OUT / name).read_bytes() == blobs[name]
    save(OUT / 'author_candidate_manifest.captured_01.json', manifest_path.read_bytes())
    for item in approved:
        assert record(ROOT / item['path']) == item
    assert record(manifest_path)['sha256'] == 'd10e90e2c7c6c3bcb4babc8f422f8564591f805a1529c448c35e7a5d5697dd72'
    snapshot = {'format': 'sn100-root-file-phase-code-snapshot-v1', 'code_only_frozen': True,
                'author_manifest': record(OUT / 'author_candidate_manifest.captured_01.json'),
                'independent_review': record(REVIEW), 'independent_tool_list': record(LIST),
                'code_records': [record(OUT / name) for name in sorted(blobs)],
                'configuration_written': False, 'phase_executed': False, 'board_operated': False}
    path = OUT / 'root_code_snapshot_14_01.json'
    save(path, (json.dumps(snapshot, ensure_ascii=False, indent=2) + '\n').encode('utf-8'))
    print(json.dumps({'snapshot': record(path), 'code_count': len(blobs), 'configuration_written': False}))


if __name__ == '__main__':
    main()
