"""总控顺序执行已冻结的文件阶段，逐步保留真实外层退出码。"""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
TOOLS = ROOT / 'temp/sn100_host/workflow_next_20261103/integration_10_root_frozen_01'
WORK = ROOT / 'temp/sn100_host/workflow_next_20261103/integration_next_02'
OUT = Path(__file__).resolve().parent / 'file_phase_run_01'


def main():
    assert (WORK / 'build_exit.txt').read_text().strip() == '0'
    complete = json.loads((WORK / 'build_complete.json').read_text())
    assert complete['arm64_build_and_package_passed'] is True
    assert complete['source_count'] == 687
    config = TOOLS / 'integration_next_tools_config.json'
    assert hashlib.sha256(config.read_bytes()).hexdigest() == '50b94b7d026385816811ae720d1409b656518dde6929a7e481b4696944462d03'
    OUT.mkdir()
    for name in ('stage_board_b_release_next.py', 'archive_integration_next.py',
                 'verify_and_save_integration_next.py', 'finish_integration_next_archive.py'):
        stem = Path(name).stem
        args = [sys.executable, '-X', 'utf8', str(TOOLS / name)]
        (OUT / (stem + '.command.json')).write_text(json.dumps(args, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        result = subprocess.run(args, cwd=ROOT, capture_output=True)
        (OUT / (stem + '.stdout.log')).write_bytes(result.stdout)
        (OUT / (stem + '.stderr.log')).write_bytes(result.stderr)
        (OUT / (stem + '.exit')).write_text(str(result.returncode) + '\n', encoding='ascii')
        print(stem + ': actual_exit=' + str(result.returncode), flush=True)
        sys.stdout.buffer.write(result.stdout)
        sys.stderr.buffer.write(result.stderr)
        sys.stdout.flush()
        sys.stderr.flush()
        if result.returncode:
            return result.returncode
    return 0


if __name__ == '__main__':
    sys.exit(main())
