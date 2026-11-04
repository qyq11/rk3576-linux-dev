"""新包文件阶段成功后，固定总控独占切版和留屏启动的实际输入；不联板。"""
from pathlib import Path
import base64
import hashlib
import json
import re

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
TOOLS = ROOT / 'temp/sn100_host/workflow_next_20261103/integration_10_root_frozen_01'
WORK = ROOT / 'temp/sn100_host/workflow_next_20261103/integration_next_02'
OUT = HERE / 'runtime_batch10_01'
CALLS = ROOT / 'temp/sn100_host/root_manager_20261103'
BASE = '/home/rkshare/programs/sn100_shell'
META = BASE + '/verification/integration_next_02_runtime_01'
OLD = 'releases/b31f581fd240e09ee4c0a4a4eb6c4cbdd674fa9cf8897fcd170e9d0ca058d993'


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def save(path, raw):
    with path.open('xb') as stream:
        stream.write(raw)


def main():
    assert (WORK / 'build_exit.txt').read_text().strip() == '0'
    for name in ('stage_board_b_release_next', 'archive_integration_next',
                 'verify_and_save_integration_next', 'finish_integration_next_archive'):
        assert (HERE / 'file_phase_run_01' / (name + '.exit')).read_text().strip() == '0'
    build_raw = (WORK / 'build_complete.json').read_bytes()
    build = json.loads(build_raw)
    assert build['source_count'] == 687 and build['arm64_build_and_package_passed'] is True
    assert build['program_executed'] is False and build['board_runtime_verified'] is False
    release = build['release_id']
    assert re.fullmatch('[a-f0-9]{64}', release)
    flat = BASE + '/releases/' + release
    config_path = TOOLS / 'integration_next_tools_config.json'
    config_sha = sha(config_path.read_bytes())
    assert config_sha == '50b94b7d026385816811ae720d1409b656518dde6929a7e481b4696944462d03'
    decision = HERE / 'lis_observation_root_decision_v1.md'
    decision_sha = sha(decision.read_bytes())
    assert decision_sha == 'c27714ee5142d4125d40235da2f551977d5e9dc4580885e34ef8755f0242be3f'
    helper_sha = sha((ROOT / '06_programs/sn100_host/shell/rk_board.sh').read_bytes())
    assert helper_sha == 'e3587bad320003bf1a2ea7b3176bf14d23527ebaae65b60ffb4bd5388d3866d2'
    original = ROOT / '04_references/sn100_host/board_b/20261103_status_login_08_permission/auto_permission_01'
    manifests = []
    for name, expected in (
        ('ini_before.sha256', '6c961ce4eff4981afa1e7b0e59711fb90eee8b45395e83e2b4049bff6fd58c38'),
        ('controller_ini_before.sha256', 'd82b216b3aee25fb0298565b73a62d7144ede9a1aae1d6840bce6da8ace3b112')):
        raw = (original / name).read_bytes()
        assert sha(raw) == expected and raw.endswith(b'\n')
        manifests.append(raw)
    configuration_manifest = b''.join(manifests)
    assert len(configuration_manifest.splitlines()) == 7
    intent = json.loads((TOOLS / 'runtime_launch_intent.template.json').read_text())
    intent.update(
        frozen=True, release_id=release, application_exe=flat + '/sn100_shell',
        application_sha256=build['binaries']['sn100_shell'],
        observation_dir=META + '/observation', observation_id='integration_next_02_lis_01',
        observation_release=release, root_decision_sha256=decision_sha,
        allow_prepare_flat=True, allow_activation=True, allow_old_app_stop=True, allow_unique_start=True,
        file_phase_config_path=config_path.relative_to(ROOT).as_posix(), file_phase_config_sha256=config_sha,
        build_complete_path=(WORK / 'build_complete.json').relative_to(ROOT).as_posix(),
        build_complete_sha256=sha(build_raw),
        package_path=(WORK / 'packages' / build['package']).relative_to(ROOT).as_posix(),
        package_sha256=build['package_sha256'])
    intent_raw = (json.dumps(intent, ensure_ascii=True, indent=2) + '\n').encode('ascii')
    approval = (
        '# Root本批运行安排\n\n'
        '用户已要求继续移植并在RK原配置、原资料、原主控验证，复核后保留程序打开。'
        '本安排由总控01a1a05a-73c4-7380-b761-c35a7fdbf76e执行，不向作者授予板卡操作。\n\n'
        f'新发布{release}，687源，完整ARM64构建及stage/archive/collect/finish真实退出均0。'
        f'文件阶段配置{config_sha}，构建终态{sha(build_raw)}，包{build["package_sha256"]}。'
        f'启动意图{sha(intent_raw)}，七配置清单{sha(configuration_manifest)}。\n\n'
        '允许先建立新平层副本，保持旧程序/current；再核身份、七配置、原主控唯一及旧运行身份后，'
        '仅为必要切版请求旧应用正常关闭一次。须取得真实干净退出、进程消失、图形服务结束及显示释放凭据后切指针；'
        '忙守卫拒绝就保留现场，不重发、不强杀、不停主控/相机。允许总控一次唯一新启动。\n\n'
        '新启动直接复用supervise BASE RELEASE RUN 0空session，通过systemd-run/openvt7/runuser lckfb/env显式三观察变量；'
        '不走旧start的30秒超时关闭分支。旧停止凭据在实际发生后单独固定，不预填。'
        'READY/观察/采集超时只返回工具失败，不结束新业务、不自动重启或回退。'
        'supervise自身异常/外部停止仍可能关闭，不能将0说成永不退出。\n\n'
        '仅被动LIS和启动软件观察，不登录、不补Qk、不发机械动作或模拟报文；'
        '不把软件项通过当真实LIS/仪器/实体/资源验收。观察结果应用退出保持null；结束另核同一活进程并留屏。\n'
    ).encode('utf-8')
    values = {
        'template_frozen': 'true', 'allow_prepare_flat': 'true', 'allow_activate': 'true',
        'release_id': release, 'expected_current': OLD,
        'expected_app_sha256': build['binaries']['sn100_shell'],
        'expected_renderer_sha256': build['binaries']['sn100_report_renderer'],
        'expected_board_script_sha256': helper_sha,
        'expected_controller_sha256': 'ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff',
        'expected_root_decision_sha256': decision_sha,
        'expected_file_phase_config_sha256': config_sha,
        'expected_build_complete_sha256': sha(build_raw),
        'expected_package_sha256': build['package_sha256'],
        'configuration_manifest': META + '/configuration.sha256',
        'configuration_manifest_sha256': sha(configuration_manifest),
        'old_stop_receipt': META + '/old_stop_receipt.txt',
        'old_stop_receipt_sha256': '0' * 64,
        'root_transition_approval': META + '/root_approval.md',
        'root_transition_approval_sha256': sha(approval),
        'root_launch_intent': META + '/runtime_launch_intent.json',
        'root_launch_intent_sha256': sha(intent_raw),
    }
    template = (TOOLS / 'transition_release_next.sh.in').read_text()
    for key, value in values.items():
        assert re.fullmatch('[A-Za-z0-9_./:-]+', value)
        literal = value if key in ('template_frozen', 'allow_prepare_flat', 'allow_activate') else "'" + value + "'"
        template, count = re.subn('^' + key + '=.*$', key + '=' + literal, template, count=1, flags=re.M)
        assert count == 1, key
    OUT.mkdir()
    for name, raw in (('runtime_launch_intent.json', intent_raw), ('root_approval.md', approval),
                      ('configuration.sha256', configuration_manifest)):
        save(OUT / name, raw)
    save(OUT / 'transition_frozen_prepare_01.sh', template.encode('utf-8'))
    save(CALLS / 'prepare_flat_batch10_01.sh', ('set -- prepare-flat\n' + template).encode('utf-8'))
    install = [
        'set -Eeuo pipefail', 'set -o noclobber', 'umask 077',
        'base=' + BASE, 'meta=' + META,
        '[[ $(cat /proc/sys/kernel/random/boot_id) == 0eb94389-f120-42ad-9042-9c21d96a1481 ]]',
        '[[ $(readlink "$base/current") == ' + OLD + ' ]]',
        '[[ ! -e $meta && ! -L $meta && -d $base/verification && ! -L $base/verification ]]',
        '[[ $(readlink -f "$base/verification") == "$base/verification" ]]',
        'mkdir -m 750 "$meta"', 'chown root:lckfb "$meta"',
        'mkdir -m 700 "$meta/observation"', 'chown lckfb:lckfb "$meta/observation"',
    ]
    for name, raw in (('runtime_launch_intent.json', intent_raw), ('root_approval.md', approval),
                      ('configuration.sha256', configuration_manifest)):
        encoded = base64.b64encode(raw).decode('ascii')
        install += [f"printf '%s' '{encoded}' | base64 -d > \"$meta/{name}\"", f'chmod 640 "$meta/{name}"',
                    f'chown root:lckfb "$meta/{name}"',
                    f"printf '%s  %s\\n' '{sha(raw)}' \"$meta/{name}\" | sha256sum -c -"]
    waiter = ROOT / '05_tools/tests/sn100_host/lis_keep_running_20261103/wait_lis_observation.sh'
    waiter_raw = waiter.read_bytes()
    assert sha(waiter_raw) == 'eff1fddabbb5d8bf89d54f3d2b45a78760e8d8a9738fd71047baa7730aad1de8'
    install += [f"printf '%s' '{base64.b64encode(waiter_raw).decode('ascii')}' | base64 -d > \"$meta/wait_lis_observation.sh\"",
                'chmod 750 "$meta/wait_lis_observation.sh"',
                f"printf '%s  %s\\n' '{sha(waiter_raw)}' \"$meta/wait_lis_observation.sh\" | sha256sum -c -",
                'sha256sum -c "$meta/configuration.sha256"',
                "printf 'RUNTIME_METADATA_INSTALLED=1\\nCURRENT_CHANGED=0\\nPROGRAM_STARTED=0\\n'" ]
    install.insert(-2, 'chown root:lckfb "$meta/wait_lis_observation.sh"')
    save(CALLS / 'install_runtime_metadata_batch10_01.sh', ('\n'.join(install) + '\n').encode('utf-8'))
    save(OUT / 'root_prepare_values.json', (json.dumps(values, indent=2) + '\n').encode('ascii'))
    print('RUNTIME_INPUTS_PREPARED=1; BOARD_EXECUTED=0')


if __name__ == '__main__':
    main()
