#!/usr/bin/env bash
# 固定这一时点的日志和运行身份；原主控继续运行，不覆盖原资料。
set -Eeuo pipefail
test "$(tr -d '\0\n' </sys/firmware/devicetree/base/serial-number)" = 8314b1e8ffd0321f
audit=/home/rkshare/programs/aca_flow_test/runs/controller_restore_20261102
fixed=$audit/fixed_$(date +%Y%m%d_%H%M%S)
mkdir -m 750 "$fixed"
live=$(cat /home/rkshare/programs/aca_flow_test/runs/correct_05/latest_live_path.txt)
case "$live" in /home/rkshare/programs/aca_flow_test/runs/correct_05/live_*) ;; *) exit 2;; esac
printf '%s\n' "$live" >"$fixed/live_path.txt"
date -Is >"$fixed/captured_at.txt"
cp -- "$live/program.log" "$fixed/restored_program_snapshot.log"
cp -- "$live/started_at.txt" "$fixed/original_started_at.txt"
cp -- "$audit/configuration_before.sha256" "$audit/original_launcher.sha256" \
    "$audit/host_current_before.txt" "$fixed/"
cp -- /home/rkshare/programs/aca_flow_test/run_correct_live.sh "$fixed/original_run_correct_live.sh"
systemctl cat sn100-original-controller-board-b.service >"$fixed/runtime_unit.txt"
systemctl show sn100-original-controller-board-b.service -p Id -p ActiveState -p SubState \
    -p MainPID -p ControlGroup -p ExecMainStartTimestamp -p ExecMainExitTimestamp \
    -p ExecMainCode -p ExecMainStatus -p Result -p Restart >"$fixed/runtime_state.txt"
sha256sum -c "$audit/configuration_before.sha256" >"$fixed/configuration_check.txt"
sha256sum /userdata/shared/config_linux.ini /userdata/shared/consume.ini \
    /userdata/shared/GL4A_config.cfg /userdata/shared/number.ini >"$fixed/configuration_after.sha256"
sha256sum /userdata/shared/ACA_V1.0OffLineD-500 \
    /home/rkshare/programs/aca_flow_test/runs/correct_05/live_20261102_173807/program.log \
    >"$fixed/original_input_hashes.txt"
journalctl _PID=38601 --no-pager -o short-iso >"$fixed/old_ssh_session.txt"
journalctl -k --since '2026-11-02 19:35:00' --no-pager -o short-iso \
    | grep -Ei 'out of memory|oom|killed process|segfault|trap|exception|fatal|watchdog|ACA|SN450' \
    >"$fixed/kernel_failure_search.txt" || true
ss -ltnp 'sport = :5000' >"$fixed/port_5000.txt"
cd "$fixed"
sha256sum ./* >SHA256SUMS
printf 'FIXED_DIRECTORY=%s\n' "$fixed"
printf 'LIVE_DIRECTORY=%s\n' "$live"
printf 'CONFIGURATION_UNCHANGED=4/4\n'
printf 'CONTROLLER_LEFT_RUNNING=1\n'
