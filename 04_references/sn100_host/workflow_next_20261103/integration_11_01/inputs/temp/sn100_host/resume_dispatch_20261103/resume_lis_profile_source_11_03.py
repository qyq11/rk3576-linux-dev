"""保留Mg已合事实及LIS首次错误工作目录记录，仅在工程根重试未执行的两源。"""
from pathlib import Path
import hashlib
import importlib.util
import json
import subprocess

ROOT = Path(__file__).resolve().parents[3]
PROGRAM = ROOT / '06_programs/sn100_host'
HERE = Path(__file__).resolve().parent
OUT = ROOT / 'temp/sn100_host/workflow_next_20261103/integration_11_source_acceptance_02'
FOLDER = OUT / 'lis_profile_source_retry_01'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path, raw):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('xb') as stream:
        stream.write(raw)


def jsave(path, value):
    save(path, (json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2) + '\n').encode('utf-8'))


def snapshot(path):
    raw = path.with_name(path.stem + '_native_raw.json')
    result = subprocess.run([str(__import__('sys').executable), '-X', 'utf8', str(PROGRAM / 'shell/package_arm64.py'), 'snapshot', '--output', str(raw)], cwd=ROOT, capture_output=True)
    save(path.with_suffix('.stdout.log'), result.stdout)
    save(path.with_suffix('.stderr.log'), result.stderr)
    save(path.with_suffix('.actual_exit'), (str(result.returncode) + '\n').encode('ascii'))
    assert result.returncode == 0
    native = json.loads(raw.read_text())
    canonical = {key.replace(chr(92), '/'): value for key, value in native.items()}
    assert len(canonical) == len(native)
    jsave(path, canonical)
    jsave(path.with_suffix('.path_normalization.json'), {'native_raw': raw.relative_to(ROOT).as_posix(),
          'native_raw_sha256': sha(raw), 'canonical': path.relative_to(ROOT).as_posix(),
          'canonical_sha256': sha(path), 'source_bytes_changed': False, 'file_count': len(canonical)})
    return canonical


def main():
    previous = OUT / 'lis_profile_source'
    assert (previous / 'check.actual_exit').read_text().strip() == '1'
    assert not (previous / 'apply.actual_exit').exists()
    mg = json.loads((OUT / 'mg04/acceptance.json').read_text())
    assert mg['all_after_bytes_equal'] is True and all(item['actual_exit'] == 0 for item in mg['commands'])
    patch = ROOT / 'temp/sn100_host/lis_profile_source_candidate_20261103_01/lis_profile_source.patch'
    assert sha(patch) == '7fd8567ece6c4f938f2acc210c2e606abecaae4fdf7b91f9870e3fdd9ad23bdf'
    FOLDER.mkdir()
    before = snapshot(FOLDER / 'source_before.json')
    assert before == json.loads((OUT / 'mg04/source_after.json').read_text()) and len(before) == 688
    commands = []
    for action, flag in (('check', ['--check']), ('apply', [])):
        args = ['git', '-c', 'core.autocrlf=false', 'apply', *flag, str(patch)]
        result = subprocess.run(args, cwd=PROGRAM, capture_output=True)
        save(FOLDER / (action + '.stdout.log'), result.stdout)
        save(FOLDER / (action + '.stderr.log'), result.stderr)
        save(FOLDER / (action + '.actual_exit'), (str(result.returncode) + '\n').encode('ascii'))
        record = {'argv': args, 'cwd': str(PROGRAM), 'actual_exit': result.returncode}
        commands.append(record)
        jsave(FOLDER / (action + '.command.json'), record)
        assert result.returncode == 0, result.stderr.decode('utf-8', errors='replace')
    after = snapshot(FOLDER / 'source_after.json')
    combo = ROOT / 'temp/sn100_host/workflow_next_20261103/root_combined_candidate_11_02'
    assert after == json.loads((combo / 'composed_expected_after_688.json').read_text())
    changed = sorted(key for key in after if before.get(key) != after[key])
    assert changed == ['lis_transport/lis_connection_controller.cpp', 'lis_transport/lis_connection_values.h']
    acceptance = {'format': 'sn100-root-real-source-patch-acceptance-v1', 'name': 'lis_profile_source',
                  'patch': patch.relative_to(ROOT).as_posix(), 'patch_sha256': sha(patch),
                  'commands': commands, 'paths': changed, 'source_files': 688,
                  'all_after_bytes_equal': True, 'source_after_sha256': sha(FOLDER / 'source_after.json'),
                  'first_wrong_cwd_record_retained': True, 'compiled': False, 'board_operated': False}
    jsave(FOLDER / 'acceptance.json', acceptance)
    result = {'format': 'sn100-root-integration-11-source-complete-v1',
              'steps': [{'name': 'mg04', 'acceptance': (OUT / 'mg04/acceptance.json').relative_to(ROOT).as_posix()},
                        {'name': 'lis_profile_source', 'acceptance': (FOLDER / 'acceptance.json').relative_to(ROOT).as_posix()}],
              'source_count': 688, 'source_after': (FOLDER / 'source_after.json').relative_to(ROOT).as_posix(),
              'source_after_sha256': sha(FOLDER / 'source_after.json'), 'complete': True,
              'scope': 'source_acceptance_only', 'compiled': False, 'board_operated': False,
              'running_release_unchanged': 'c7cbf438'}
    jsave(OUT / 'source_complete.json', result)
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
