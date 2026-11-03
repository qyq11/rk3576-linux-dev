"""从已完成合入和冻结配置启动一次整版ARM64编译，原样保存WSL输出。"""
from pathlib import Path
from datetime import datetime, timezone
import argparse
import hashlib
import json
import subprocess

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config-sha256', required=True)
    args = parser.parse_args()
    task = Path(__file__).resolve().parent
    root = task.parents[2]
    tools = task / 'integration_08_tool_candidate'
    config = tools / 'integration_08_tools_config.json'
    assert hashlib.sha256(config.read_bytes()).hexdigest() == args.config_sha256
    assert json.loads(config.read_text(encoding='utf-8'))['frozen'] is True
    progress = json.loads((task / 'integration_08_review/promotion_progress.json').read_text(encoding='utf-8'))
    assert progress['complete'] is True and progress['phase'] == 'complete'
    assert not (task / 'integration_08').exists()
    dispatch = task / 'integration_08_build_dispatch'
    dispatch.mkdir()
    wsl_root = '/mnt/c/' + root.as_posix()[3:]
    command = ['wsl.exe', '-d', 'Ubuntu-24.04', '-u', 'qyq', '--cd', wsl_root,
               '--', '/bin/bash', (tools / 'build_integration_08.sh').relative_to(root).as_posix()]
    with (dispatch / 'started.json').open('x', encoding='utf-8') as out:
        json.dump({'started_at_utc': datetime.now(timezone.utc).isoformat(),
                   'command': command, 'tools_config_sha256': args.config_sha256},
                  out, ensure_ascii=False, indent=2)
        out.write('\n')
    with (dispatch / 'execution.log').open('xb') as out:
        result = subprocess.run(command, cwd=root, stdout=out, stderr=subprocess.STDOUT)
    with (dispatch / 'execution.exit').open('x', encoding='ascii') as out:
        out.write(str(result.returncode) + '\n')
    print('BUILD_08_ACTUAL_EXIT=' + str(result.returncode))
    raise SystemExit(result.returncode)

if __name__ == '__main__':
    main()
