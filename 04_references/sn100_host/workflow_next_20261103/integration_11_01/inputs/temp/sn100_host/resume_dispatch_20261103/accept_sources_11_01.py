"""Root顺序采纳已审Mg十五项和LIS两项，逐步保留真实退出与完整后像。"""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
PROGRAM = ROOT / '06_programs/sn100_host'
HERE = Path(__file__).resolve().parent
COMBO = ROOT / 'temp/sn100_host/workflow_next_20261103/root_combined_candidate_11_02'
OUT = ROOT / 'temp/sn100_host/workflow_next_20261103/integration_11_source_acceptance_01'
BASE = ROOT / 'temp/sn100_host/workflow_next_20261103/integration_10_source_acceptance_01/ascii_b1/source_after.json'
LIS_ROOT = ROOT / 'temp/sn100_host/lis_profile_source_candidate_20261103_01'
LIS = {'lis_transport/lis_connection_values.h', 'lis_transport/lis_connection_controller.cpp'}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path, raw):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('xb') as stream:
        stream.write(raw)


def json_save(path, data):
    save(path, (json.dumps(data, ensure_ascii=False, indent=2, sort_keys=True) + '\n').encode('utf-8'))


def actual_snapshot(path):
    script = PROGRAM / 'shell/package_arm64.py'
    result = subprocess.run([sys.executable, '-X', 'utf8', str(script), 'snapshot', '--output', str(path)], cwd=ROOT, capture_output=True)
    save(path.with_suffix('.stdout.log'), result.stdout)
    save(path.with_suffix('.stderr.log'), result.stderr)
    save(path.with_suffix('.actual_exit'), (str(result.returncode) + '\n').encode('ascii'))
    assert result.returncode == 0
    return json.loads(path.read_text())


def main():
    assert sha(BASE) == 'f741feea28fcb6a2bcff8d6c2bda49faa67d3b72e3508062e5f1e368b2422b8b'
    before = json.loads(BASE.read_text())
    assert len(before) == 687 and not (PROGRAM / 'reagents/original_reagent_probe.h').exists()
    patch_path = COMBO / 'packaging_01/mg04_lis_profile_on_f741.patch'
    assert sha(patch_path) == 'cabddb143bedcf0602b1a40627b1501a66993c1c989673a215a57eda3dbc8f74'
    # 四份组合独审及原两片审必须均已交付；真实文件SHA另随每片记录固定。
    reviews = [HERE / name for name in ('combined_11_business_review_02.md', 'combined_11_lifecycle_review_02.md',
                'reagent_probe_04_business_targeted_review_01.md', 'reagent_probe_final_lifecycle_review_01.md',
                'lis_profile_source_independent_review_01.md')]
    assert all(path.is_file() for path in reviews)
    OUT.mkdir()
    current = actual_snapshot(OUT / 'source_before.json')
    assert current == before
    chunks = patch_path.read_bytes().split(b'diff --git ')
    assert chunks[0] == b''
    mg_chunks, lis_chunks = [], []
    mg_paths, lis_paths = [], []
    for chunk in chunks[1:]:
        header = chunk.splitlines()[0].decode('ascii')
        a, b = header.split(' ')
        assert a.startswith('a/06_programs/sn100_host/') and b == 'b/' + a[2:]
        name = a.removeprefix('a/06_programs/sn100_host/')
        if name in LIS:
            lis_chunks.append(b'diff --git ' + chunk); lis_paths.append(name)
        else:
            mg_chunks.append(b'diff --git ' + chunk); mg_paths.append(name)
    assert len(mg_paths) == 15 and set(lis_paths) == LIS
    mg_patch = OUT / 'mg04_on_f741_root_revision02.patch'
    save(mg_patch, b''.join(mg_chunks))
    # LIS仍采用作者原普通补丁，不能把Root新split文本冒称作者原7fd证据。
    lis_patch = LIS_ROOT / 'lis_profile_source.patch'
    assert sha(lis_patch) == '7fd8567ece6c4f938f2acc210c2e606abecaae4fdf7b91f9870e3fdd9ad23bdf'
    expected_final = json.loads((COMBO / 'composed_expected_after_688.json').read_text())
    steps = []
    for name, patch, paths in (('mg04', mg_patch, mg_paths), ('lis_profile_source', lis_patch, sorted(LIS))):
        folder = OUT / name
        folder.mkdir()
        expected = dict(current)
        for path in paths:
            expected[path] = sha(COMBO / 'candidate/06_programs/sn100_host' / path)
        json_save(folder / 'expected_after.json', expected)
        json_save(folder / 'source_before.json', current)
        record = {'format': 'sn100-root-real-source-patch-acceptance-v1', 'name': name,
                  'time_utc': datetime.now(timezone.utc).isoformat(), 'patch': patch.relative_to(ROOT).as_posix(),
                  'patch_sha256': sha(patch), 'paths': paths,
                  'reviews': [{'path': path.relative_to(ROOT).as_posix(), 'sha256': sha(path)} for path in reviews],
                  'commands': []}
        for action, suffix in (('check', ['--check']), ('apply', [])):
            args = ['git', '-c', 'core.autocrlf=false', 'apply', *suffix, str(patch)]
            result = subprocess.run(args, cwd=ROOT, capture_output=True)
            save(folder / (action + '.stdout.log'), result.stdout)
            save(folder / (action + '.stderr.log'), result.stderr)
            save(folder / (action + '.actual_exit'), (str(result.returncode) + '\n').encode('ascii'))
            record['commands'].append({'argv': args, 'actual_exit': result.returncode})
            json_save(folder / (action + '.command.json'), record['commands'][-1])
            assert result.returncode == 0, result.stderr.decode('utf-8', errors='replace')
        after = actual_snapshot(folder / 'source_after.json')
        record['source_files'] = len(after)
        record['all_after_bytes_equal'] = after == expected
        record['source_after_sha256'] = sha(folder / 'source_after.json')
        json_save(folder / 'acceptance.json', record)
        assert after == expected
        steps.append({'name': name, 'patch_sha256': record['patch_sha256'],
                      'check_actual_exit': 0, 'apply_actual_exit': 0,
                      'source_after_sha256': record['source_after_sha256'], 'source_count': len(after)})
        current = after
        print(json.dumps(steps[-1]), flush=True)
    assert current == expected_final and len(current) == 688
    result = {'format': 'sn100-root-integration-11-source-complete-v1', 'steps': steps,
              'source_count': 688, 'complete': True, 'scope': 'source_acceptance_only',
              'compiled': False, 'board_operated': False, 'running_release_unchanged': 'c7cbf438'}
    json_save(OUT / 'source_complete.json', result)
    print('FORMAL_SOURCE_688_ACCEPTED=1; COMPILED=0; BOARD_OPERATED=0; RUNNING_C7_UNCHANGED=1')


if __name__ == '__main__':
    main()
