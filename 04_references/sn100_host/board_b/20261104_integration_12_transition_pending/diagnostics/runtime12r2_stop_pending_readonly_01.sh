set -Eeuo pipefail
base=/home/rkshare/programs/sn100_shell
meta=$base/verification/integration_12_01_runtime_02
old=$base/runs/20261103_221819_662833
printf 'FORMAT=sn100-runtime12r2-stop-pending-diagnosis-v1\nUTC='; date -u +%Y-%m-%dT%H:%M:%SZ
printf 'BOOT_ID='; cat /proc/sys/kernel/random/boot_id
sha256sum "$meta/configuration.sha256"
sha256sum -c "$meta/configuration.sha256"
for name in original_project_root instrument_configuration_file theme_configuration_file; do sha256sum "$base/$name"; done
printf 'CURRENT='; readlink "$base/current"
printf 'LATEST_RUN='; cat "$base/latest_run"
for name in stdout.log stderr.log actual_exit; do
    path=$meta/phase_logs/stop/$name
    printf 'STOP_TOOL_FILE=%s\n' "$name"
    if [[ -f $path ]]; then sha256sum "$path"; head -c 1048576 "$path"; printf '\n'; else printf 'MISSING\n'; fi
done
for name in result.txt exit_code app.pid app.start_ticks app.exe boot_id; do
    printf 'OLD_RUN_FILE=%s\n' "$name"
    if [[ -f $old/$name ]]; then cat "$old/$name"; else printf 'MISSING\n'; fi
done
if [[ -r /proc/669382/stat ]]; then
    printf 'OLD_APPLICATION_STILL_PRESENT=1\n'
    cat /proc/669382/stat
    readlink /proc/669382/exe || true
    sha256sum /proc/669382/exe || true
    awk '/^Name:|^State:|^Uid:|^VmRSS:|^Threads:/{print}' /proc/669382/status
else printf 'OLD_APPLICATION_STILL_PRESENT=0\n'; fi
printf 'OLD_APPLICATION_LIFECYCLE_LINES_BEGIN\n'
tail -n 160 "$old/app.log" | sed -n '/CLOSE/p;/SHUTDOWN/p;/STOP/p;/THREAD/p;/PENDING/p;/GUARD/p;/BUSY/p;/DRAFT/p;/STORE_CLOSED/p'
printf 'OLD_APPLICATION_LIFECYCLE_LINES_END\n'
systemctl show sn100-shell.service opto-camera.service -p Id -p ActiveState -p SubState -p MainPID -p Result -p ExecMainStatus
printf 'READ_ONLY=1\nNO_ADDITIONAL_CLOSE_REQUEST=1\nNO_RESTART=1\n'
