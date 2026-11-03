#!/usr/bin/env bash
set -Eeuo pipefail
[[ $(tr -d '\0\n' </proc/device-tree/serial-number) == 8314b1e8ffd0321f ]]
base=/home/rkshare/programs/sn100_shell
release=$base/releases/b31f581fd240e09ee4c0a4a4eb6c4cbdd674fa9cf8897fcd170e9d0ca058d993
[[ $(readlink -f "$base/current") == "$release" ]]
[[ $(sha256sum "$release/sn100_shell" | cut -d' ' -f1) == 5b108ad35472709fccde35c4cc63d4be64637dcfc7e087839122a548c548d238 ]]
for pointer in original_project_root instrument_configuration_file theme_configuration_file;do
 [[ -f $base/$pointer && ! -L $base/$pointer ]]
done
data=$base/runtime/data
[[ $(cat "$base/original_project_root") == "$data" && $(readlink -f "$data") == "$data" ]]
[[ $(cat "$base/instrument_configuration_file") == "$data/config.ini" ]]
[[ $(cat "$base/theme_configuration_file") == "$data/theme_config.ini" ]]
[[ -f $data/.sn100-original-work-v1 && ! -L $data/.sn100-original-work-v1 && $(cat "$data/.sn100-original-work-v1") == sn100-original-work-v1 ]]
[[ $(sha256sum /home/rkshare/programs/sn100_shell/verification/status_login_08_permission_20261103/runs/auto_permission_01/ini_before.sha256 | cut -d' ' -f1) == 6c961ce4eff4981afa1e7b0e59711fb90eee8b45395e83e2b4049bff6fd58c38 ]]
sha256sum -c /home/rkshare/programs/sn100_shell/verification/status_login_08_permission_20261103/runs/auto_permission_01/ini_before.sha256
[[ $(sha256sum /home/rkshare/programs/sn100_shell/verification/status_login_08_permission_20261103/runs/auto_permission_01/controller_ini_before.sha256 | cut -d' ' -f1) == d82b216b3aee25fb0298565b73a62d7144ede9a1aae1d6840bce6da8ace3b112 ]]
sha256sum -c /home/rkshare/programs/sn100_shell/verification/status_login_08_permission_20261103/runs/auto_permission_01/controller_ini_before.sha256

controller=
for item in /proc/[0-9]*/exe;do
 path=$(readlink "$item" || true)
 case "$path" in
  /userdata/shared/ACA_V1.0OffLineD-500) [[ -z $controller ]];controller=${item#/proc/};controller=${controller%/exe};;
  */sn100_shell|*/sn100_shell" (deleted)"|*/status_login*review*|*/sn100_report_renderer) printf 'APPLICATION_ALREADY_RUNNING=%s\n' "$item";exit 3;;
 esac
done
[[ -n $controller && $(sha256sum "/proc/$controller/exe" | cut -d' ' -f1) == ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff ]]
ss -H -ltnp 'sport = :5000' | grep -q "pid=$controller,"
printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\nCONTROLLER_PID=%s\nUSER_REQUESTED_KEEP_OPEN=1\n' "$controller"
runuser -u lckfb -- /bin/bash "$release/rk_board.sh" check "$base"
previous=$(cat "$base/latest_run" 2>/dev/null || true)
# 0表示没有自动关闭时限；此处不启动或重启原主控。
runuser -u lckfb -- /bin/bash "$release/rk_board.sh" start "$base" 0
runuser -u lckfb -- /bin/bash "$release/rk_board.sh" status "$base"
runuser -u lckfb -- /bin/bash -c '
 set -Eeuo pipefail
 source "$1/rk_board.sh"
 run=$(latest_run "$2")
 [[ ${run##*/} != "$3" && ! -e $run/exit_code ]]
 [[ $(cat "$run/release_id") == "${1##*/}" && $(cat "$run/app.exe") == "$1/sn100_shell" ]]
 process_matches "$run"
 grep -qx SHELL_READY "$run/app.log"
 [[ $(sha256sum "/proc/$(cat "$run/app.pid")/exe" | cut -d" " -f1) == "$4" ]]
 printf "FORMAL_RUNNING_RUN=%s\nFORMAL_PROGRAM_LEFT_RUNNING=1\nAPP_READY=1\n" "$run"
' keep_open_guard "$release" "$base" "$previous" 5b108ad35472709fccde35c4cc63d4be64637dcfc7e087839122a548c548d238
[[ $(readlink "/proc/$controller/exe") == /userdata/shared/ACA_V1.0OffLineD-500 ]]
printf 'CONTROLLER_RESTARTED=0\n'
