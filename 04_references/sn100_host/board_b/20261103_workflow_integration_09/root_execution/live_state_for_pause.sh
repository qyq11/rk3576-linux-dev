#!/usr/bin/env bash
# 只读记录正式界面、原主控与同板相机背景；不关闭、重启或发送操作命令。
set -Eeuo pipefail
base=/home/rkshare/programs/sn100_shell
id=b31f581fd240e09ee4c0a4a4eb6c4cbdd674fa9cf8897fcd170e9d0ca058d993
release=$base/releases/$id
[[ $(readlink -f "$base/current") == "$release" ]]
date -Is
printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\nBOOT_ID=';cat /proc/sys/kernel/random/boot_id
runuser -u lckfb -- /bin/bash -c '
 set -Eeuo pipefail
 source "$1/rk_board.sh"
 run=$(latest_run "$2")
 [[ "$run" == "$2/runs/20261103_180023_24243" && ! -e $run/exit_code ]]
 process_matches "$run"
 grep -qx SHELL_READY "$run/app.log"
 [[ $(sha256sum "/proc/$(cat "$run/app.pid")/exe" | cut -d" " -f1) == 5b108ad35472709fccde35c4cc63d4be64637dcfc7e087839122a548c548d238 ]]
 printf "FORMAL_RUN=%s\nFORMAL_PID=%s\nFORMAL_RUNNING=1\nFORMAL_READY=1\n" "$run" "$(cat "$run/app.pid")"
' pause_audit "$release" "$base"
controller=
for item in /proc/[0-9]*/exe;do
 if [[ $(readlink "$item" || true) == /userdata/shared/ACA_V1.0OffLineD-500 ]];then
  [[ -z $controller ]];controller=${item#/proc/};controller=${controller%/exe}
 fi
done
[[ -n $controller && $(sha256sum "/proc/$controller/exe" | cut -d' ' -f1) == ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff ]]
ss -H -ltnp 'sport = :5000' | grep -q "pid=$controller,"
printf 'CONTROLLER_PID=%s\nCONTROLLER_ORIGINAL_ALIVE=1\n' "$controller"
before=$base/verification/status_login_08_permission_20261103/runs/auto_permission_01
[[ $(sha256sum "$before/ini_before.sha256" | cut -d' ' -f1) == 6c961ce4eff4981afa1e7b0e59711fb90eee8b45395e83e2b4049bff6fd58c38 ]]
[[ $(sha256sum "$before/controller_ini_before.sha256" | cut -d' ' -f1) == d82b216b3aee25fb0298565b73a62d7144ede9a1aae1d6840bce6da8ace3b112 ]]
sha256sum -c "$before/ini_before.sha256"
sha256sum -c "$before/controller_ini_before.sha256"
printf 'CAMERA_SERVICE_BEGIN\n'
systemctl show opto-camera.service -p ActiveState -p SubState -p MainPID -p Nice -p MemoryCurrent -p CPUUsageNSec -p ActiveEnterTimestamp
printf 'CAMERA_SERVICE_END\nREAD_ONLY=1\nPROGRAMS_STARTED_STOPPED_OR_RESTARTED=0\n'
