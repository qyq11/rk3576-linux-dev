"""Root逐项采用已审九源与三处构建登记；保存真实前后像，不联板。"""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
PROGRAM = ROOT / '06_programs/sn100_host'
COMBO = ROOT / 'temp/sn100_host/workflow_next_20261103/root_lis_a_candidate_13_01'
OUT = ROOT / 'temp/sn100_host/workflow_next_20261103/integration_13_source_acceptance_01'
BASE = ROOT / 'temp/sn100_host/workflow_next_20261103/integration_12_source_acceptance_01/ascii_uh/source_after.json'

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
    assert not raw_path.exists()
    argv = [sys.executable, '-B', '-X', 'utf8', str(PROGRAM / 'shell/package_arm64.py'),
            'snapshot', '--output', str(raw_path)]
    result = subprocess.run(argv, cwd=ROOT, capture_output=True)
    save(path.with_suffix('.stdout.log'), result.stdout)
    save(path.with_suffix('.stderr.log'), result.stderr)
    save(path.with_suffix('.actual_exit'), (str(result.returncode) + '\n').encode('ascii'))
    json_save(path.with_suffix('.command.json'), {'argv': argv, 'cwd': str(ROOT), 'actual_exit': result.returncode})
    assert result.returncode == 0, result.stderr
    native = json.loads(raw_path.read_bytes())
    canonical = {name.replace(chr(92), '/'): value for name, value in native.items()}
    assert len(canonical) == len(native)
    for name in canonical:
        assert not name.startswith('/') and ':' not in name and all(p not in ('', '.', '..') for p in name.split('/'))
    json_save(path, canonical)
    json_save(path.with_suffix('.path_normalization.json'), {
        'native_raw': raw_path.relative_to(ROOT).as_posix(), 'native_raw_sha256': sha(raw_path),
        'canonical': path.relative_to(ROOT).as_posix(), 'canonical_sha256': sha(path),
        'source_bytes_changed': False, 'file_count': len(canonical)})
    return canonical

def main():
    assert not OUT.exists()
    assert sha(BASE) == '0e5c70ef9d5ac5fa3db34888103b4158783141aac5f90845e225c720d532ac8d'
    assert sha(ROOT / '.gitattributes') == 'c46d8032dc7311b5911dfb7e578ad70ba1258ac924b72675f8b2192c5b9d445a'
    assert sha(ROOT / '04_references/sn100_host/board_b/20261104_integration_12_transition_pending/SHA256SUMS') == '9bf2ef0708a0cc3404833d35d0382664867aaad2c9f5785f21ecfc4a739c5e42'
    previous = ROOT / 'temp/sn100_host/workflow_next_20261103/integration_12_01'
    assert json.loads((previous / 'finish_complete.json').read_bytes())['complete'] is True
    assert (previous / 'logs/root_finish_01.exit').read_text().strip() == '0'
    patch = COMBO / 'lis_application_plan_A9_on_0e5c.patch'
    assert sha(patch) == 'f0050002c3355ea80c2e8845fb9515443f641439bf549bee872ab6249f007d5a'
    changes_path = COMBO / 'changes_12.json'
    assert sha(changes_path) == '88dc03bc06ac9fd9ba2bf1e17cb43b3d226363a11754512c759c76448e6dec5b'
    expected_path = COMBO / 'expected_virtual_697_manifest.json'
    assert sha(expected_path) == 'ed4a3eb3025ef0602cdb49d41a6094f35011eb68ac19cf9f36e4f75fb3f6e254'
    base, expected = json.loads(BASE.read_bytes()), json.loads(expected_path.read_bytes())
    changes = json.loads(changes_path.read_bytes())
    assert len(base) == 688 and len(expected) == 697 and len(changes) == 12
    modified = {x['path'].removeprefix('06_programs/sn100_host/') for x in changes if x['operation'] == 'modify'}
    added = {x['path'].removeprefix('06_programs/sn100_host/') for x in changes if x['operation'] == 'add'}
    assert modified == {'shell/shell.pri', 'shell/build.sh', 'shell/package_arm64.py'}
    assert len(added) == 9 and added == set(expected) - set(base) and all(x.startswith('lis_business/') for x in added)
    assert {name for name in base if base[name] != expected[name]} == modified
    patch_names = []
    for chunk in patch.read_bytes().split(b'diff --git ')[1:]:
        a, b = chunk.splitlines()[0].decode('ascii').split(' ')
        assert a.startswith('a/06_programs/sn100_host/') and b == 'b/' + a[2:]
        patch_names.append(a.removeprefix('a/06_programs/sn100_host/'))
    assert len(patch_names) == 12 and set(patch_names) == modified | added
    for change in changes:
        path = change['path']
        relative = path.removeprefix('06_programs/sn100_host/')
        assert sha(COMBO / 'candidate' / path) == change['after_sha256'] == expected[relative]
        if relative in modified:
            assert sha(ROOT / path) == sha(COMBO / 'base' / path) == change['before_sha256'] == base[relative]
        else:
            assert not (ROOT / path).exists()
            assert sha(ROOT / 'temp/sn100_host/lis_application_plan_candidate_20261104_01/candidate' / relative) == expected[relative]
    reviews = [HERE / name for name in ('root_lis_a_13_public_files_review_01.md',
               'lis_application_plan_business_review_01.md', 'lis_application_plan_resource_lifecycle_review_01.md')]
    assert sha(reviews[0]) == 'b2aba8f51ce4d98d734240dc1a380baac3fca1e85d7f0625f59425f2c627bf08'
    assert sha(reviews[1]) == 'ae1a92410b4e38b70f5307097f84b09078582aa04e6f76e38a8fae4bc9e52679'
    assert reviews[2].is_file() and reviews[2].stat().st_size > 500
    OUT.mkdir()
    actual_before = actual_snapshot(OUT / 'source_before.json')
    assert actual_before == base
    folder = OUT / 'lis_a'
    folder.mkdir()
    json_save(folder / 'source_before.json', actual_before)
    json_save(folder / 'expected_after.json', expected)
    commands = []
    for name, suffix in (('check', ['--check']), ('apply', [])):
        argv = ['git', '-c', 'core.autocrlf=false', 'apply', *suffix, str(patch)]
        result = subprocess.run(argv, cwd=ROOT, capture_output=True)
        save(folder / (name + '.stdout.log'), result.stdout)
        save(folder / (name + '.stderr.log'), result.stderr)
        save(folder / (name + '.actual_exit'), (str(result.returncode) + '\n').encode('ascii'))
        command = {'argv': argv, 'cwd': str(ROOT), 'actual_exit': result.returncode}
        json_save(folder / (name + '.command.json'), command)
        commands.append(command)
        assert result.returncode == 0, result.stderr.decode('utf-8', errors='replace')
    actual_after = actual_snapshot(folder / 'source_after.json')
    acceptance = {'format': 'sn100-root-real-source-patch-acceptance-v1', 'name': 'lis_a',
                  'time_utc': datetime.now(timezone.utc).isoformat(), 'patch': patch.relative_to(ROOT).as_posix(),
                  'patch_sha256': sha(patch), 'paths': sorted(modified | added), 'commands': commands,
                  'reviews': [{'path': p.relative_to(ROOT).as_posix(), 'sha256': sha(p)} for p in reviews],
                  'source_files': len(actual_after), 'all_after_bytes_equal': actual_after == expected,
                  'source_after_sha256': sha(folder / 'source_after.json')}
    json_save(folder / 'acceptance.json', acceptance)
    assert actual_after == expected and len(actual_after) == 697
    result = {'format': 'sn100-root-integration-13-source-complete-v1', 'source_count': 697,
              'scope': 'source_acceptance_only', 'complete': True, 'modified': sorted(modified),
              'added': sorted(added), 'deleted': [], 'check_actual_exit': 0, 'apply_actual_exit': 0,
              'source_after_sha256': acceptance['source_after_sha256'], 'compiled': False, 'board_operated': False,
              'running_release_unchanged': 'c7cbf438', 'runtime12_status': 'close_guard_rejected_stop4',
              'lis_production_enabled': False, 'emergency_recovery_source': 'user_deferred_todo'}
    json_save(OUT / 'source_complete.json', result)
    print(json.dumps(result, ensure_ascii=False))

if __name__ == '__main__':
    main()
