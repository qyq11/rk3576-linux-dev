#!/usr/bin/env bash
# 板 B 重启后沿既有授权恢复唯一原主控；不改配置，不打开上位机。
set -Eeuo pipefail
umask 027
test "$(tr -d '\0\n' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
test "$(cat /proc/sys/kernel/random/boot_id)" = a3556b56-e03d-488c-84db-cf80acb461a0
test "$(sha256sum /userdata/shared/ACA_V1.0OffLineD-500 | cut -d' ' -f1)" = ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff
unit=sn100-original-controller-board-b.service
test "$(systemctl show "$unit" -p LoadState --value)" = not-found
for exe in /proc/[0-9]*/exe; do
    path=$(readlink "$exe" || true)
    case "$path" in
        */ACA_V1.0OffLineD-500|*/ACA_V1.0D-500|*/sn100_shell|*/status_login*review*) printf 'ALREADY_RUNNING=%s\n' "$path"; exit 2 ;;
    esac
done
test -z "$(ss -H -ltn 'sport = :5000')"
test "$(readlink /home/rkshare/programs/sn100_shell/current)" = releases/08bc43a73a9e8e4826a559f501829355357a46a999799e8b662ebd1feae1a0ef
sha256sum -c /home/rkshare/programs/aca_flow_test/runs/controller_restore_20261102/configuration_before.sha256
sha256sum -c /home/rkshare/programs/aca_flow_test/runs/controller_restore_20261102/original_launcher.sha256
audit=/home/rkshare/programs/aca_flow_test/runs/controller_resume_20261103_1544
test ! -e "$audit"
install -d -m 750 "$audit"
date -Is >"$audit/requested_at.txt"
sha256sum /userdata/shared/config_linux.ini /userdata/shared/consume.ini /userdata/shared/GL4A_config.cfg /userdata/shared/number.ini >"$audit/configuration_before.sha256"
find /home/rkshare/programs/sn100_shell/runtime/data -maxdepth 1 -type f -iname '*.ini' -print0 | sort -z | xargs -0 -r sha256sum >>"$audit/configuration_before.sha256"
systemd-run --unit="$unit" --description='Board B original controller resumed after reboot' \
    --service-type=exec --property=Restart=no --property=KillMode=control-group \
    --property=WorkingDirectory=/userdata/shared --property=UMask=0027 \
    /usr/bin/bash /home/rkshare/programs/aca_flow_test/run_correct_live.sh
systemctl show "$unit" -p Id -p LoadState -p ActiveState -p SubState -p MainPID -p ExecMainStartTimestamp -p Result -p Restart | tee "$audit/service_after_start.txt"
printf 'AUDIT_DIRECTORY=%s\nHOST_NOT_STARTED=1\n' "$audit"
