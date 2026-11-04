"""Root窄采用已冻结的样本两源；实收git及源码枚举结果，不操作板卡。"""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
PROGRAM = ROOT / '06_programs/sn100_host'
COPY = HERE / 'root_sample_add_14_review_01'
OUT = ROOT / 'temp/sn100_host/workflow_next_20261103/integration_14_source_acceptance_01'
BASE = ROOT / 'temp/sn100_host/workflow_next_20261103/integration_13_source_acceptance_01/lis_a/source_after.json'
PINS = {
    'root_sample_add_14_review_01/root_snapshot.json': '143bbb6ceb6150c998e462c1fedf56011b6938dddf7c835d48801ec849d60a18',
    'sample14_behavior_review_01.md': 'c5af94111a5552d7dcdff7d94f5692f56cda868b8de775e7d613017ddc32e03d',
    'sample14_lifecycle_review_01.md': '5746bb71aee6dfaf501ae116481dafd7b67e27fd1c003e637ad5ba203f0fb9e4',
}
AFTER = {'samples/sample_page.cpp': '2e29097f023b728ac87790eae04bec6eb602060d48344536744be43850b16c39',
         'samples/sample_page.h': '41a464c372627abd79ff5997154618d47a96ffbcb834d4051d07f9f78ba65a22'}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path, raw):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('xb') as stream:
        stream.write(raw)


def json_save(path, data):
    save(path, (json.dumps(data, ensure_ascii=False, indent=2, sort_keys=True) + '\n').encode('utf-8'))


def rec(path):
    return {'path': path.relative_to(ROOT).as_posix(), 'sha256': sha(path), 'bytes': path.stat().st_size}


def command(path, argv, phase):
    result = subprocess.run(argv, cwd=ROOT, capture_output=True)
    save(path.with_suffix('.stdout.log'), result.stdout)
    save(path.with_suffix('.stderr.log'), result.stderr)
    save(path.with_suffix('.actual_exit'), (str(result.returncode) + '\n').encode('ascii'))
    note = {'phase': phase, 'argv': argv, 'cwd': str(ROOT), 'actual_exit': result.returncode}
    json_save(path.with_suffix('.command.json'), note)
    assert result.returncode == 0, result.stderr.decode('utf-8', errors='replace')
    return note


def actual_snapshot(path):
    native = path.with_name(path.stem + '_native_raw.json')
    assert not native.exists()
    command(path, [sys.executable, '-B', '-X', 'utf8', str(PROGRAM / 'shell/package_arm64.py'),
                   'snapshot', '--output', str(native)], 'source_inventory')
    value = json.loads(native.read_bytes())
    canonical = {name.replace(chr(92), '/'): item for name, item in value.items()}
    assert len(value) == len(canonical)
    assert all(not name.startswith('/') and ':' not in name and
               all(part not in ('', '.', '..') for part in name.split('/')) for name in canonical)
    json_save(path, canonical)
    json_save(path.with_suffix('.path_normalization.json'), {
        'native_raw': rec(native), 'canonical': rec(path),
        'source_bytes_changed': False, 'file_count': len(canonical)})
    return canonical


def main():
    assert not OUT.exists()
    assert sha(BASE) == 'ed4a3eb3025ef0602cdb49d41a6094f35011eb68ac19cf9f36e4f75fb3f6e254'
    assert sha(ROOT / '.gitattributes') == 'c46d8032dc7311b5911dfb7e578ad70ba1258ac924b72675f8b2192c5b9d445a'
    for name, expected in PINS.items():
        assert sha(HERE / name) == expected, name
    snapshot = json.loads((COPY / 'root_snapshot.json').read_bytes())
    assert snapshot['review_only'] and snapshot['formal_source_paths_verified'] == 697
    assert len(snapshot['records']) == 6
    for row in snapshot['records']:
        path = ROOT / row['copy']['path']
        assert rec(path) == row['copy'] and path.resolve().is_relative_to(COPY.resolve())
    base = json.loads(BASE.read_bytes())
    assert len(base) == 697
    expected = dict(base)
    expected.update(AFTER)
    assert {name for name in base if base[name] != expected[name]} == set(AFTER)
    for name, value in AFTER.items():
        assert sha(COPY / 'candidate' / name) == value
        assert sha(PROGRAM / name) == base[name]
    patch = COPY / 'sample_add_order.patch'
    assert sha(patch) == '85d9aadc9b92e1f1df790b356a92bf8c8c1fa55458b0a904cd7c5ce6d7243928'
    paths = []
    for chunk in patch.read_bytes().split(b'diff --git ')[1:]:
        a, b = chunk.splitlines()[0].decode('ascii').split(' ')
        assert a.startswith('a/06_programs/sn100_host/') and b == 'b/' + a[2:]
        paths.append(a.removeprefix('a/06_programs/sn100_host/'))
    assert len(paths) == 2 and set(paths) == set(AFTER)
    OUT.mkdir()
    before = actual_snapshot(OUT / 'source_before.json')
    assert before == base
    folder = OUT / 'sample_add_order'
    folder.mkdir()
    json_save(folder / 'source_before.json', before)
    json_save(folder / 'expected_after.json', expected)
    json_save(folder / 'changes_2.json', [
        {'path': '06_programs/sn100_host/' + name, 'operation': 'modify',
         'before_sha256': before[name], 'after_sha256': AFTER[name]}
        for name in sorted(AFTER)])
    commands = []
    for name, args in (('check', ['--check']), ('apply', [])):
        commands.append(command(folder / name,
            ['git', '-c', 'core.autocrlf=false', 'apply', '--whitespace=error-all', *args, str(patch)], name))
    after = actual_snapshot(folder / 'source_after.json')
    note = {'format': 'sn100-root-real-source-patch-acceptance-v1', 'name': 'sample_add_order',
            'time_utc': datetime.now(timezone.utc).isoformat(), 'patch': patch.relative_to(ROOT).as_posix(),
            'patch_sha256': sha(patch), 'paths': sorted(AFTER), 'commands': commands,
            'reviews': [rec(HERE / name) for name in PINS], 'source_files': len(after),
            'all_after_bytes_equal': after == expected, 'source_after_sha256': sha(folder / 'source_after.json')}
    json_save(folder / 'acceptance.json', note)
    assert after == expected and len(after) == 697
    result = {'format': 'sn100-root-integration-14-source-complete-v1', 'source_count': len(after),
              'complete': True, 'scope': 'source_acceptance_only', 'modified': sorted(AFTER),
              'added': [], 'deleted': [], 'check_actual_exit': 0, 'apply_actual_exit': 0,
              'source_after_sha256': note['source_after_sha256'], 'compiled': False,
              'board_operated': False, 'running_release_unchanged': 'c7cbf438',
              'lis_production_enabled': False, 'emergency_recovery_source': 'user_deferred_todo'}
    json_save(OUT / 'source_complete.json', result)
    print(json.dumps(result, ensure_ascii=False))


if __name__ == '__main__':
    main()
