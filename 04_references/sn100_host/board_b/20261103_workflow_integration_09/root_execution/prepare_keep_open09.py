"""用户要求复核后保留正式界面；只生成一次正式启动脚本，不重复启动主控。"""
from pathlib import Path
import hashlib
import json
here=Path(__file__).resolve().parent
root=here.parents[2]
work=root/'temp/sn100_host/workflow_night_20261102/integration_09_revision_05'
assert (here/'run09_revision05.exit').read_text().strip()=='0'
assert (here/'independent_review09_revision05.exit').read_text().strip()=='0'
build=json.loads((work/'build_complete.json').read_text())
release=build['release_id'];app=build['binaries']['sn100_shell']
checks=''
for name in ('ini_before.sha256','controller_ini_before.sha256'):
    saved=root/'04_references/sn100_host/board_b/20261103_status_login_08_permission/auto_permission_01'/name
    pin=hashlib.sha256(saved.read_bytes()).hexdigest()
    remote='/home/rkshare/programs/sn100_shell/verification/status_login_08_permission_20261103/runs/auto_permission_01/'+name
    checks+=f'[[ $(sha256sum {remote} | cut -d\' \' -f1) == {pin} ]]\nsha256sum -c {remote}\n'
script=f'''#!/usr/bin/env bash
set -Eeuo pipefail
[[ $(tr -d '\\0\\n' </proc/device-tree/serial-number) == 8314b1e8ffd0321f ]]
base=/home/rkshare/programs/sn100_shell
release=$base/releases/{release}
[[ $(readlink -f "$base/current") == "$release" ]]
[[ $(sha256sum "$release/sn100_shell" | cut -d' ' -f1) == {app} ]]
for pointer in original_project_root instrument_configuration_file theme_configuration_file;do
 [[ -f $base/$pointer && ! -L $base/$pointer ]]
done
data=$base/runtime/data
[[ $(cat "$base/original_project_root") == "$data" && $(readlink -f "$data") == "$data" ]]
[[ $(cat "$base/instrument_configuration_file") == "$data/config.ini" ]]
[[ $(cat "$base/theme_configuration_file") == "$data/theme_config.ini" ]]
[[ -f $data/.sn100-original-work-v1 && ! -L $data/.sn100-original-work-v1 && $(cat "$data/.sn100-original-work-v1") == sn100-original-work-v1 ]]
{checks}
controller=
for item in /proc/[0-9]*/exe;do
 path=$(readlink "$item" || true)
 case "$path" in
  /userdata/shared/ACA_V1.0OffLineD-500) [[ -z $controller ]];controller=${{item#/proc/}};controller=${{controller%/exe}};;
  */sn100_shell|*/sn100_shell" (deleted)"|*/status_login*review*|*/sn100_report_renderer) printf 'APPLICATION_ALREADY_RUNNING=%s\\n' "$item";exit 3;;
 esac
done
[[ -n $controller && $(sha256sum "/proc/$controller/exe" | cut -d' ' -f1) == ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff ]]
ss -H -ltnp 'sport = :5000' | grep -q "pid=$controller,"
printf 'BOARD=board_b\\nSERIAL=8314b1e8ffd0321f\\nCONTROLLER_PID=%s\\nUSER_REQUESTED_KEEP_OPEN=1\\n' "$controller"
runuser -u lckfb -- /bin/bash "$release/rk_board.sh" check "$base"
previous=$(cat "$base/latest_run" 2>/dev/null || true)
# 0表示没有自动关闭时限；此处不启动或重启原主控。
runuser -u lckfb -- /bin/bash "$release/rk_board.sh" start "$base" 0
runuser -u lckfb -- /bin/bash "$release/rk_board.sh" status "$base"
runuser -u lckfb -- /bin/bash -c '
 set -Eeuo pipefail
 source "$1/rk_board.sh"
 run=$(latest_run "$2")
 [[ ${{run##*/}} != "$3" && ! -e $run/exit_code ]]
 [[ $(cat "$run/release_id") == "${{1##*/}}" && $(cat "$run/app.exe") == "$1/sn100_shell" ]]
 process_matches "$run"
 grep -qx SHELL_READY "$run/app.log"
 [[ $(sha256sum "/proc/$(cat "$run/app.pid")/exe" | cut -d" " -f1) == "$4" ]]
 printf "FORMAL_RUNNING_RUN=%s\\nFORMAL_PROGRAM_LEFT_RUNNING=1\\nAPP_READY=1\\n" "$run"
' keep_open_guard "$release" "$base" "$previous" {app}
[[ $(readlink "/proc/$controller/exe") == /userdata/shared/ACA_V1.0OffLineD-500 ]]
printf 'CONTROLLER_RESTARTED=0\\n'
'''
with (here/'keep_open09.sh').open('x',encoding='utf-8',newline='\n') as out:out.write(script)
print('KEEP_OPEN_SCRIPT_PREPARED=1; PROGRAM_NOT_YET_STARTED=1')
