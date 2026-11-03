#!/usr/bin/env bash
# 仅恢复板B已有正确主控；不改配置，不启动上位机，不发送仪器命令。
set -Eeuo pipefail
test "$(tr -d '\0\n' </sys/firmware/devicetree/base/serial-number)" = 8314b1e8ffd0321f
test "$(sha256sum /userdata/shared/ACA_V1.0OffLineD-500 | cut -d' ' -f1)" = ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff
unit=sn100-original-controller-board-b.service
audit=/home/rkshare/programs/aca_flow_test/runs/controller_restore_20261102
mkdir -p "$audit"
chmod 750 "$audit"
for exe in /proc/[0-9]*/exe; do
    path=$(readlink "$exe" || true)
    case "$path" in
        */ACA_V1.0OffLineD-500|*/ACA_V1.0D-500)
            printf 'ALREADY_RUNNING=%s\n' "$exe"; exit 1 ;;
    esac
done
if ss -H -ltn 'sport = :5000' | grep -q .; then
    printf 'PORT_5000_ALREADY_IN_USE=1\n'; exit 1
fi
if systemctl is-active --quiet "$unit"; then
    printf 'RESTORE_UNIT_ALREADY_ACTIVE=1\n'; exit 1
fi
date -Is >"$audit/restore_requested_at.txt"
sha256sum /userdata/shared/config_linux.ini /userdata/shared/consume.ini \
    /userdata/shared/GL4A_config.cfg /userdata/shared/number.ini >"$audit/configuration_before.sha256"
sha256sum /home/rkshare/programs/aca_flow_test/run_correct_live.sh >"$audit/original_launcher.sha256"
readlink -f /home/rkshare/programs/sn100_shell/current >"$audit/host_current_before.txt"
systemd-run --unit="$unit" --description='Board B original controller restored by user request' \
    --service-type=exec --property=Restart=no --property=KillMode=control-group \
    --property=WorkingDirectory=/userdata/shared --property=UMask=0027 \
    /usr/bin/bash /home/rkshare/programs/aca_flow_test/run_correct_live.sh
systemctl show "$unit" -p Id -p LoadState -p ActiveState -p SubState -p MainPID \
    -p ExecMainStartTimestamp -p ExecMainCode -p ExecMainStatus -p Result -p Restart -p FragmentPath \
    | tee "$audit/service_after_start.txt"
printf 'AUDIT_DIRECTORY=%s\n' "$audit"
printf 'ORIGINAL_LAUNCHER_UNCHANGED=1\n'
printf 'HOST_NOT_STARTED=1\n'
printf 'NO_INSTRUMENT_COMMAND_SENT=1\n'
