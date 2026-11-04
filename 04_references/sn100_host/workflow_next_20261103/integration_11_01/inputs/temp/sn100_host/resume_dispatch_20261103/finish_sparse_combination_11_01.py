"""保留B1与Mg各自撤销入口，生成稀疏组合普通补丁并在私有前像实际核对。"""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'temp/sn100_host/workflow_next_20261103/root_combined_candidate_11_01'
CURRENT = ROOT / '06_programs/sn100_host'
BEFORE = OUT / 'before'
CANDIDATE = OUT / 'candidate'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path, raw):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('xb') as stream:
        stream.write(raw)


def main():
    source = CANDIDATE / '06_programs/sn100_host/shell/device_control_shell.cpp'
    raw = source.read_bytes()
    lines = raw.splitlines(keepends=True)
    start = [i for i, line in enumerate(lines) if line.startswith(b'<<<<<<< ')]
    base = [i for i, line in enumerate(lines) if line.startswith(b'||||||| ')]
    sep = [i for i, line in enumerate(lines) if line.rstrip(b'\r\n') == b'=======']
    end = [i for i, line in enumerate(lines) if line.startswith(b'>>>>>>> ')]
    assert len(start) == len(base) == len(sep) == len(end) == 1
    a, b, c, d = start[0], base[0], sep[0], end[0]
    assert a < b < c < d and b + 1 == c
    ours = b''.join(lines[a+1:b])
    theirs = b''.join(lines[c+1:d])
    assert len(ours.splitlines()) == len(theirs.splitlines()) == 1
    assert b'cancelManualCommunication(' in ours and b'cancelReagentProbe(' in theirs
    resolved = b''.join(lines[:a]) + ours + theirs + b''.join(lines[d+1:])
    assert b'<<<<<<<' not in resolved and b'>>>>>>>' not in resolved
    save(OUT / 'merge/device_control_shell.raw_conflict.cpp', raw)
    save(OUT / 'merge/resolution_delta.diff',
         b'Root resolution: keep current B1 cancelManualCommunication, then Mg04 cancelReagentProbe, then existing cancelTaskReagentContexts. No existing guard removed.\n')
    source.write_bytes(resolved)
    root_manifest = ROOT / 'temp/sn100_host/workflow_next_20261103/integration_10_source_acceptance_01/ascii_b1/source_after.json'
    expected = json.loads(root_manifest.read_text())
    assert len(expected) == 687
    selected = {}
    for path in sorted((CANDIDATE / '06_programs/sn100_host').rglob('*')):
        if path.is_file():
            name = path.relative_to(CANDIDATE / '06_programs/sn100_host').as_posix()
            assert not any(marker in path.read_bytes() for marker in (b'<<<<<<< current_', b'>>>>>>> Mg_revision'))
            selected[name] = sha(path)
            expected[name] = selected[name]
    assert len(selected) == 17 and len(expected) == 688
    save(OUT / 'sparse_after_17.json', (json.dumps(selected, indent=2) + '\n').encode('utf-8'))
    save(OUT / 'composed_expected_after_688.json', (json.dumps(expected, indent=2, sort_keys=True) + '\n').encode('utf-8'))
    pack = OUT / 'packaging_01'
    pack.mkdir()
    work, replay = pack / 'private_diff', pack / 'ordinary_replay'
    work.mkdir(); replay.mkdir()
    commands = []

    def run(args, cwd, acceptable=(0,)):
        result = subprocess.run(args, cwd=cwd, capture_output=True)
        stem = str(len(commands) + 1).zfill(2)
        save(pack / (stem + '.stdout.log'), result.stdout)
        save(pack / (stem + '.stderr.log'), result.stderr)
        commands.append({'argv': args, 'cwd': str(cwd), 'actual_exit': result.returncode})
        save(pack / (stem + '.actual_exit'), (str(result.returncode) + '\n').encode('ascii'))
        assert result.returncode in acceptable, (args, result.returncode, result.stderr.decode('utf-8', errors='replace'))
        return result.stdout

    for folder in (work, replay):
        run(['git', '-c', 'core.autocrlf=false', 'init', '--quiet'], folder)
        for path in BEFORE.rglob('*'):
            if path.is_file():
                save(folder / path.relative_to(BEFORE), path.read_bytes())
    run(['git', '-c', 'core.autocrlf=false', 'add', '--', '06_programs'], work)
    for path in CANDIDATE.rglob('*'):
        if path.is_file():
            target = work / path.relative_to(CANDIDATE)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(path.read_bytes())
    run(['git', '-c', 'core.autocrlf=false', 'add', '-N', '--', '06_programs/sn100_host/reagents/original_reagent_probe.h'], work)
    run(['git', '-c', 'core.autocrlf=false', '-c', 'core.whitespace=trailing-space,space-before-tab,cr-at-eol', 'diff', '--check'], work)
    patch = run(['git', '-c', 'core.autocrlf=false', 'diff', '--binary', '--full-index', '--no-ext-diff', '--src-prefix=a/', '--dst-prefix=b/'], work)
    assert patch
    patch_path = pack / 'mg04_lis_profile_on_f741.patch'
    save(patch_path, patch)
    run(['git', '-c', 'core.autocrlf=false', 'apply', '--check', str(patch_path)], replay)
    run(['git', '-c', 'core.autocrlf=false', 'apply', str(patch_path)], replay)
    for name, expected_sha in selected.items():
        assert sha(replay / '06_programs/sn100_host' / name) == expected_sha, name
    # 组合只是私有17片；正式687实物逐份仍是原f741，不称已晋级或已编译。
    original = json.loads(root_manifest.read_text())
    for name, expected_sha in original.items():
        assert sha(CURRENT / name) == expected_sha, name
    save(pack / 'commands.json', (json.dumps(commands, indent=2) + '\n').encode('utf-8'))
    result = {'format': 'sn100-root-sparse-combination-replay-v1',
              'time_utc': datetime.now(timezone.utc).isoformat(), 'base_count': 687,
              'sparse_files': 17, 'modified': 16, 'added': ['reagents/original_reagent_probe.h'],
              'patch_sha256': sha(patch_path), 'sparse_after_sha256': sha(OUT / 'sparse_after_17.json'),
              'composed_expected_688_sha256': sha(OUT / 'composed_expected_after_688.json'),
              'ordinary_check_actual_exit': 0, 'ordinary_apply_actual_exit': 0,
              'replayed_17_exact_bytes_equal': True, 'formal_687_unchanged': True,
              'formal_written': False, 'compiled': False, 'board_operated': False}
    save(pack / 'acceptance.json', (json.dumps(result, indent=2) + '\n').encode('utf-8'))
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
