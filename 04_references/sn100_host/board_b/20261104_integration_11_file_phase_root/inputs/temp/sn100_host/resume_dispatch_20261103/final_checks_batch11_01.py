"""仅核本段源码、归档原字节和文档差异，不执行产品或连接板卡。"""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
WORK = ROOT / 'temp/sn100_host/workflow_next_20261103/integration_11_01'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check_fixed(folder, count):
    rows = (folder / 'SHA256SUMS').read_text().splitlines()
    assert len(rows) == count
    listed = set()
    for row in rows:
        digest, name = row.split('  ', 1)
        path = folder / name
        assert path.resolve().is_relative_to(folder.resolve()) and not path.is_symlink()
        assert name not in listed and sha(path) == digest, name
        listed.add(name)
    assert listed == {p.relative_to(folder).as_posix() for p in folder.rglob('*')
                      if p.is_file() and p != folder / 'SHA256SUMS'}


def main():
    manifest = ROOT / 'temp/sn100_host/workflow_next_20261103/integration_11_source_acceptance_02/lis_profile_source_retry_01/source_after.json'
    assert sha(manifest) == 'b0788f899b63a982c55ae85d098d7f4e843bdbedbd19b9f7c1ca0d5a3c569767'
    expected = json.loads(manifest.read_text())
    assert len(expected) == 688
    for name, digest in expected.items():
        path = ROOT / '06_programs/sn100_host' / name
        assert not path.is_symlink() and sha(path) == digest, name
    check_fixed(ROOT / '04_references/sn100_host/board_b/20261103_integration_10_keep_running', 139)
    check_fixed(ROOT / '04_references/sn100_host/workflow_next_20261103/integration_11_01', 202)
    check_fixed(ROOT / '07_backups/sn100_host/board_b/workflow_integration_20261103/integration_11_01', 9)
    assert sha(ROOT / '.gitattributes') == 'c46d8032dc7311b5911dfb7e578ad70ba1258ac924b72675f8b2192c5b9d445a'
    assert sha(ROOT / 'temp/sn100_host/workflow_next_20261103/integration_11_root_frozen_01/integration_11_01_tools_config.json') == '051977b52473c907e3df8dba3245781432aa619b79cb0a9f4b22b2430b92186c'
    for name in ('build_exit.txt', 'upload.exit', 'board_b_stage.exit'):
        assert (WORK / name).read_text().strip() == '0'
    for phase in ('stage', 'archive', 'collect', 'finish'):
        assert (WORK / f'logs/root_{phase}_01.exit').read_text().strip() == '0'
    finish = json.loads((WORK / 'finish_complete.json').read_text())
    assert finish['complete'] is True and finish['current_changed'] is False and finish['program_started'] is False
    assert finish['rk_function_verified'] is False and finish['instrument_verified'] is False
    git = subprocess.run(['git', '-c', 'core.autocrlf=false', '-c',
                          'core.whitespace=trailing-space,space-before-tab,cr-at-eol',
                          'diff', '--check'], cwd=ROOT, capture_output=True)
    for suffix, raw in (('.stdout.log', git.stdout), ('.stderr.log', git.stderr),
                        ('.exit', (str(git.returncode) + '\n').encode())):
        with (HERE / ('final_git_diff_check_11_01' + suffix)).open('xb') as stream:
            stream.write(raw)
    assert git.returncode == 0
    result = {'format': 'sn100-batch11-final-file-check-v1', 'time_utc': datetime.now(timezone.utc).isoformat(),
              'source_count': 688, 'current_equals_b078': True, 'old_runtime_139_bytes_equal': True,
              'new_fixed_evidence_202_bytes_equal': True, 'recovery_9_bytes_equal': True,
              'root_attributes_and_frozen_config_unchanged': True, 'build_and_four_phases_actual_exit': 0,
              'git_diff_check_actual_exit': git.returncode, 'product_executed': False, 'board_operated': False}
    with (HERE / 'final_checks_batch11_01.json').open('x', encoding='utf-8', newline='\n') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
