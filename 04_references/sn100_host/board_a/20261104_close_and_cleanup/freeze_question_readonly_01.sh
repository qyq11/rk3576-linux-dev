#!/usr/bin/env bash
base=/home/rkshare/programs/sn100_shell
run=$base/runs/20261104_153722_5015
[[ $(cat /proc/sys/kernel/random/boot_id) == 559493ce-5319-42b6-9383-bf7b93764a35 ]]
printf 'ACTUAL_INI_FINGERPRINTS\n'
sha256sum "$base/runtime/data/config.ini" "$base/runtime/data/config_Debug.ini" "$base/runtime/data/theme_config.ini" /userdata/shared/config_linux.ini /userdata/shared/consume.ini /userdata/shared/GL4A_config.cfg /userdata/shared/number.ini
printf 'CURRENT=%s\nACTIVE_VT=%s\n' "$(readlink -f "$base/current")" "$(cat /sys/class/tty/tty0/active)"
for sample in 1 2; do
    printf 'SAMPLE=%s TIME=%s\n' "$sample" "$(date -Is)"
    for pid in 5836 4426; do
        if [[ -e /proc/$pid/exe ]];then
            printf 'PID=%s EXE=%s\n' "$pid" "$(readlink /proc/$pid/exe)"
            cat /proc/$pid/stat
            printf 'MAIN_WCHAN=';cat /proc/$pid/wchan;printf '\n'
            awk '/^State:|^Threads:|^VmRSS:|^voluntary_ctxt_switches:|^nonvoluntary_ctxt_switches:/' /proc/$pid/status
        else printf 'PID_ABSENT=%s\n' "$pid";fi
    done
    stat -c 'APP_LOG_BYTES=%s APP_LOG_MODIFIED=%y' "$run/app.log"
    if [[ $sample == 1 ]];then sleep 2;fi
done
printf 'GUI_THREAD_WAIT_POINTS\n'
for task in /proc/5836/task/[0-9]*;do
    printf '%s ' "${task##*/}";cat "$task/comm";cat "$task/wchan";printf '\n'
done
printf 'RECENT_NON_SENSITIVE_MARKERS\n'
grep -E 'SAMPLE_ADD_REVIEW|SHELL_READY|SHUTDOWN_|STORE_CLOSED|THREADS_STOPPED|ORIGINAL_AD_CHANNELS|DEVICE.*(ERROR|STATE)|^.*(Segmentation|ASSERT|deadlock)' "$run/app.log" | tail -15 || true
printf 'CURRENT_PROCESS_SOCKET_STATE\n'
ss -H -tnp '( sport = :5000 or dport = :5000 )'
printf 'MAIN_RUN_END_FILES\n'
for name in exit_code result.txt;do if [[ -f $run/$name ]];then cat "$run/$name";else printf 'ABSENT=%s\n' "$name";fi;done
printf 'KERNEL_WARNINGS_CURRENT_BOOT\n'
journalctl -k -b --no-pager -n 60 | grep -Ei 'oom|out of memory|killed process|segfault|hung task|blocked for|gpu|drm.*(error|fail)' | tail -8 || true
printf 'READONLY_DIAGNOSIS_COMPLETE_NO_INPUT_OR_SIGNAL=1\n'
