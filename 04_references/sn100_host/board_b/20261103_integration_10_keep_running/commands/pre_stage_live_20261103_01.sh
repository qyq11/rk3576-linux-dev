set -Eeuo pipefail
base=/home/rkshare/programs/sn100_shell
[[ $(cat /proc/sys/kernel/random/boot_id) == 0eb94389-f120-42ad-9042-9c21d96a1481 ]]
[[ $(readlink "$base/current") == releases/b31f581fd240e09ee4c0a4a4eb6c4cbdd674fa9cf8897fcd170e9d0ca058d993 ]]
ini="$base/verification/status_login_08_permission_20261103/runs/auto_permission_01/ini_before.sha256"
controller_ini="$base/verification/status_login_08_permission_20261103/runs/auto_permission_01/controller_ini_before.sha256"
printf '%s  %s\n' 6c961ce4eff4981afa1e7b0e59711fb90eee8b45395e83e2b4049bff6fd58c38 "$ini" | sha256sum -c -
printf '%s  %s\n' d82b216b3aee25fb0298565b73a62d7144ede9a1aae1d6840bce6da8ace3b112 "$controller_ini" | sha256sum -c -
sha256sum -c "$ini"
sha256sum -c "$controller_ini"
test -f "$base/runtime/data/.sn100-original-work-v1"
run_id=$(cat "$base/latest_run")
[[ $run_id =~ ^[0-9_]+$ ]]
run="$base/runs/$run_id"
[[ -d $run && ! -L $run && ! -e $run/exit_code ]]
pid=$(cat "$run/app.pid")
[[ $pid == 24414 && $(readlink "/proc/$pid/exe") == "$base/$(readlink "$base/current")/sn100_shell" ]]
printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\nUTC='; date -u +%Y-%m-%dT%H:%M:%SZ
printf 'OLD_RUN=%s\nAPP_PID=%s\nSTART_TICKS=' "$run" "$pid"; cat "$run/app.start_ticks"
printf 'OLD_APP_SHA='; sha256sum "/proc/$pid/exe" | cut -d' ' -f1
systemctl show sn100-shell.service sn100-original-controller-board-b.service opto-camera.service -p Id -p ActiveState -p SubState -p MainPID
ss -lntp '( sport = :5000 or sport = :8080 )'
printf 'PRE_STAGE_CONFIG_AND_EXISTING_RUN_VERIFIED=1\n'
