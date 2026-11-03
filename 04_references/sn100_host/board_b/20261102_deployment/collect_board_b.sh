#!/usr/bin/env bash
# 只收回程序日志、退出结果与环境信息，不复制数据库或认证文件。
set -Eeuo pipefail
umask 077
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
base=/home/rkshare/programs/sn100_shell
out=$base/verification/board_b_20261102_evidence
mkdir -p "$out"
{
    printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\n'
    date -Is
    uname -a
    cat /etc/os-release
    id
    timedatectl show -p Timezone -p NTPSynchronized -p NTP
    printf 'HDMI_STATUS='; cat /sys/class/drm/card0-HDMI-A-1/status
    printf 'HDMI_MODES\n'; cat /sys/class/drm/card0-HDMI-A-1/modes
    printf 'CURRENT_RELEASE='; readlink "$base/current"
    sha256sum "$base/current/sn100_shell" "$base/current/sn100_report_renderer" "$base/sn100.sh"
    printf 'SYSTEM_QT\n'
    dpkg-query -W libqt5core5t64 libqt5gui5t64 libqt5widgets5t64 libegl1 libgbm1
    printf 'DISPLAY_OWNERS\n'
    sudo -n fuser -v /dev/dri/card0 /dev/tty7 2>&1 || true
    printf 'SERVICE\n'
    systemctl show sn100-shell.service -p ActiveState -p SubState
    printf 'CURRENT_RUN='; cat "$base/latest_run"
    printf 'STATUS\n'
    bash "$base/sn100.sh" status
    run=$base/runs/$(cat "$base/latest_run")
    pid=$(cat "$run/app.pid")
    printf 'ACTIVE_APPLICATION\n'
    ps -p "$pid" -o pid,user,etime,pcpu,rss,nlwp,comm
    printf 'APPLICATION_CONNECTIONS\n'
    ss -tpn | grep "pid=$pid," || true
    printf 'RTC\n'
    for node in /sys/class/rtc/rtc*/name; do test ! -f "$node" || cat "$node"; done
} > "$out/final_environment.txt"
for run in "$base"/runs/*; do
    test -d "$run" || continue
    name=${run##*/}
    mkdir -p "$out/screen_runs/$name"
    for file in app.log supervisor.log runtime.txt resources.tsv release_id app.pid app.start_ticks app.exe boot_id exit_code result.txt kms.json; do
        if [[ -f $run/$file && ! -L $run/$file ]]; then cp "$run/$file" "$out/screen_runs/$name/$file"; fi
    done
done
mkdir -p "$out/offscreen"
for file in environment.txt app_1.log app_2.log resources_1.tsv resources_2.tsv exit_1.txt exit_2.txt result.txt; do
    cp "$base/verification/board_b_20261102_offscreen/$file" "$out/offscreen/$file"
done
cp "$base/deploy_checks/dependencies.txt" "$out/dependencies.txt"
tar -czf "$base/verification/board_b_20261102_evidence.tar.gz" -C "$out" .
printf 'EVIDENCE_READY=1\n'
