set -Eeuo pipefail
base=/home/rkshare/programs/sn100_shell
meta=$base/verification/integration_12_01_runtime_01
printf 'FORMAT=sn100-runtime12-refusal-diagnosis-v1\nUTC='; date -u +%Y-%m-%dT%H:%M:%SZ
printf 'BOOT_ID='; cat /proc/sys/kernel/random/boot_id
printf 'ACTUAL_CONFIGURATION_CHECK_BEGIN\n'
sha256sum "$meta/configuration.sha256"
sha256sum -c "$meta/configuration.sha256"
for pointer in original_project_root instrument_configuration_file theme_configuration_file; do sha256sum "$base/$pointer"; done
printf 'ACTUAL_CONFIGURATION_CHECK_END\nCURRENT='; readlink "$base/current"
printf 'LATEST_RUN='; cat "$base/latest_run"
for pid in 669382 5008 2046481 139148; do
    if [[ -r /proc/$pid/stat ]]; then
        printf 'IDENTITY_PID=%s\n' "$pid"
        cat "/proc/$pid/stat"
        awk '/^Uid:/{print}' "/proc/$pid/status"
        readlink "/proc/$pid/exe" || true
        sha256sum "/proc/$pid/exe" || true
    else printf 'EXPECTED_PID_MISSING=%s\n' "$pid"; fi
done
printf 'MATCHING_EXECUTABLES_BEGIN\n'
for node in /proc/[0-9]*/exe; do
    value=$(readlink "$node" 2>/dev/null || true)
    case ${value##*/} in sn100_shell*|ACA_V1.0OffLineD-500*|opto-camera*|rkaiq_3A_server*) printf '%s %s\n' "$node" "$value";; esac
done
printf 'MATCHING_EXECUTABLES_END\n'
for phase in metadata prepare preflight stop; do
    for leaf in authorizations/$phase.json receipts/$phase.receipt phase_logs/$phase/stdout.log phase_logs/$phase/stderr.log phase_logs/$phase/actual_exit; do
        if [[ -f $meta/$leaf ]]; then printf 'REMOTE_PHASE_FILE=%s\n' "$leaf"; stat -c '%s bytes' "$meta/$leaf"; sha256sum "$meta/$leaf"; fi
    done
done
old=$base/runs/20261103_221819_662833
for leaf in app.pid app.start_ticks app.exe boot_id result.txt exit_code; do
    if [[ -f $old/$leaf ]]; then printf 'OLD_RUN_FILE=%s\n' "$leaf"; cat "$old/$leaf"; else printf 'OLD_RUN_FILE_MISSING=%s\n' "$leaf"; fi
done
systemctl show sn100-shell.service opto-camera.service -p Id -p ActiveState -p SubState -p MainPID -p Result -p ExecMainStatus
ss -H -lntp 'sport = :5000'
printf 'READ_ONLY=1\nPROGRAM_SIGNALLED=0\nCURRENT_CHANGED=0\n'
