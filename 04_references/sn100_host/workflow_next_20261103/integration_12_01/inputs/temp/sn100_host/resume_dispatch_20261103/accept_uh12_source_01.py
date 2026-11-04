"""逐片采用已双审的精确Uh六文件；保留真实命令、完整前后像和退出。"""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
PROGRAM = ROOT / '06_programs/sn100_host'
COMBO = ROOT / 'temp/sn100_host/workflow_next_20261103/root_ascii_uh_candidate_12_01'
OUT = ROOT / 'temp/sn100_host/workflow_next_20261103/integration_12_source_acceptance_01'
BASE = ROOT / 'temp/sn100_host/workflow_next_20261103/integration_11_source_acceptance_02/lis_profile_source_retry_01/source_after.json'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path, raw):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('xb') as stream:
        stream.write(raw)


def json_save(path, data):
    save(path, (json.dumps(data, ensure_ascii=False, indent=2, sort_keys=True) + '\n').encode('utf-8'))


def actual_snapshot(path):
    raw_path = path.with_name(path.stem + '_native_raw.json')
    args = [sys.executable, '-X', 'utf8', str(PROGRAM / 'shell/package_arm64.py'),
            'snapshot', '--output', str(raw_path)]
    result = subprocess.run(args, cwd=ROOT, capture_output=True)
    save(path.with_suffix('.stdout.log'), result.stdout)
    save(path.with_suffix('.stderr.log'), result.stderr)
    save(path.with_suffix('.actual_exit'), (str(result.returncode) + '\n').encode())
    json_save(path.with_suffix('.command.json'), {'argv': args, 'cwd': str(ROOT), 'actual_exit': result.returncode})
    assert result.returncode == 0
    native = json.loads(raw_path.read_text(encoding='utf-8'))
    canonical = {name.replace(chr(92), '/'): value for name, value in native.items()}
    assert len(canonical) == len(native)
    for name in canonical:
        assert not name.startswith('/') and ':' not in name and all(part not in ('', '.', '..') for part in name.split('/'))
    json_save(path, canonical)
    json_save(path.with_suffix('.path_normalization.json'), {
        'native_raw': raw_path.relative_to(ROOT).as_posix(), 'native_raw_sha256': sha(raw_path),
        'canonical': path.relative_to(ROOT).as_posix(), 'canonical_sha256': sha(path),
        'source_bytes_changed': False, 'file_count': len(canonical)})
    return canonical


def main():
    assert not OUT.exists()
    assert sha(BASE) == 'b0788f899b63a982c55ae85d098d7f4e843bdbedbd19b9f7c1ca0d5a3c569767'
    previous = ROOT / 'temp/sn100_host/workflow_next_20261103/integration_11_01'
    assert json.loads((previous / 'finish_complete.json').read_text())['complete'] is True
    assert (previous / 'logs/root_finish_01.exit').read_text().strip() == '0'
    patch = COMBO / 'packaging_01/ascii_uh_on_b078.patch'
    assert sha(patch) == 'd89e920d59e6fde53cb87a1290dc1a67483fb072fd38321071a9799d3ba71c15'
    sparse = COMBO / 'sparse_after_6.json'
    expected_file = COMBO / 'composed_expected_after_688.json'
    assert sha(sparse) == '67d0067d5b266435c82b775f078fe17ee5367bdcbfa7a0e2a58670d819dd218f'
    assert sha(expected_file) == '0e5c70ef9d5ac5fa3db34888103b4158783141aac5f90845e225c720d532ac8d'
    after_six = json.loads(sparse.read_text())
    assert len(after_six) == 6
    base = json.loads(BASE.read_text())
    expected = json.loads(expected_file.read_text())
    assert len(base) == len(expected) == 688
    assert {k for k in expected if expected[k] != base[k]} == set(after_six)
    changed = []
    for chunk in patch.read_bytes().split(b'diff --git ')[1:]:
        a, b = chunk.splitlines()[0].decode('ascii').split(' ')
        assert a.startswith('a/06_programs/sn100_host/') and b == 'b/' + a[2:]
        changed.append(a.removeprefix('a/06_programs/sn100_host/'))
    assert len(changed) == 6 and set(changed) == set(after_six)
    for name, digest in after_six.items():
        assert sha(COMBO / 'candidate/06_programs/sn100_host' / name) == digest
        assert sha(COMBO / 'before/06_programs/sn100_host' / name) == base[name]
    reviews = [('ascii_uh_root_combined_business_review_12_01.md', 'f424f1d1a5db11c41deaf982276b8f6c4353dbdccdd6dd8d5826b631818ff32c'),
               ('ascii_uh_root_combined_lifecycle_review_12_01.md', '42c3fb139d3b0453bf50ca522469aab3e62370cccade730078bb794950624c3a')]
    assert all(sha(HERE / name) == digest for name, digest in reviews)
    assert sha(ROOT / '.gitattributes') == 'c46d8032dc7311b5911dfb7e578ad70ba1258ac924b72675f8b2192c5b9d445a'
    OUT.mkdir()
    actual_before = actual_snapshot(OUT / 'source_before.json')
    assert actual_before == base
    folder = OUT / 'ascii_uh'
    folder.mkdir()
    json_save(folder / 'source_before.json', actual_before)
    json_save(folder / 'expected_after.json', expected)
    commands = []
    for name, suffix in (('check', ['--check']), ('apply', [])):
        args = ['git', '-c', 'core.autocrlf=false', 'apply', *suffix, str(patch)]
        result = subprocess.run(args, cwd=ROOT, capture_output=True)
        save(folder / (name + '.stdout.log'), result.stdout)
        save(folder / (name + '.stderr.log'), result.stderr)
        save(folder / (name + '.actual_exit'), (str(result.returncode) + '\n').encode())
        command = {'argv': args, 'cwd': str(ROOT), 'actual_exit': result.returncode}
        json_save(folder / (name + '.command.json'), command)
        commands.append(command)
        assert result.returncode == 0, result.stderr.decode('utf-8', errors='replace')
    actual_after = actual_snapshot(folder / 'source_after.json')
    record = {'format': 'sn100-root-real-source-patch-acceptance-v1', 'name': 'ascii_uh',
              'time_utc': datetime.now(timezone.utc).isoformat(), 'patch': patch.relative_to(ROOT).as_posix(),
              'patch_sha256': sha(patch), 'paths': sorted(after_six), 'commands': commands,
              'reviews': [{'path': (HERE / name).relative_to(ROOT).as_posix(), 'sha256': digest} for name, digest in reviews],
              'source_files': len(actual_after), 'all_after_bytes_equal': actual_after == expected,
              'source_after_sha256': sha(folder / 'source_after.json')}
    json_save(folder / 'acceptance.json', record)
    assert actual_after == expected and len(actual_after) == 688
    result = {'format': 'sn100-root-integration-12-source-complete-v1', 'source_count': 688,
              'scope': 'source_acceptance_only', 'complete': True, 'modified': sorted(after_six),
              'added': [], 'deleted': [], 'check_actual_exit': 0, 'apply_actual_exit': 0,
              'source_after_sha256': record['source_after_sha256'], 'compiled': False, 'board_operated': False,
              'running_release_unchanged': 'c7cbf438', 'emergency_recovery_source': 'user_deferred_todo'}
    json_save(OUT / 'source_complete.json', result)
    print(json.dumps(result, ensure_ascii=False))


if __name__ == '__main__':
    main()
