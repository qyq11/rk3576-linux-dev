#!/usr/bin/env bash
base=/home/rkshare/programs/sn100_shell
printf 'BOARD=board_a\nSERIAL=%s\nBOOT=%s\n' "$(tr -d '\0\n' </proc/device-tree/serial-number)" "$(cat /proc/sys/kernel/random/boot_id)"
id
uname -a
printf 'ACTIVE_VT=%s\n' "$(cat /sys/class/tty/tty0/active)"
if [[ -L $base/current ]]; then printf 'CURRENT=%s\n' "$(readlink -f "$base/current")"; fi
if [[ -f $base/latest_run ]]; then printf 'LATEST_RUN=%s\n' "$(cat "$base/latest_run")"; fi
for name in original_project_root instrument_configuration_file theme_configuration_file; do
    if [[ -f $base/$name ]]; then
        printf 'POINTER=%s\n' "$base/$name"
        cat "$base/$name"
        printf '\n'
        sha256sum "$base/$name"
    else
        printf 'POINTER_ABSENT=%s\n' "$name"
    fi
done
for directory in /proc/[0-9]*; do
    executable=$(readlink "$directory/exe" 2>/dev/null) || continue
    case "$executable" in
        "$base/"*|*/ACA_V1.0OffLineD-500*|*/sn100_shell|*/'sn100_shell (deleted)')
            printf 'PROCESS=%s EXE=%s\n' "${directory##*/}" "$executable"
            cat "$directory/stat" 2>/dev/null || true
            awk '/^Uid:|^Threads:/' "$directory/status" 2>/dev/null || true
            sha256sum "$directory/exe" 2>/dev/null || true
            ;;
    esac
done
systemctl show sn100-shell.service lightdm.service opto-camera.service -p Id -p LoadState -p ActiveState -p SubState -p MainPID -p UnitFileState
ss -lntp | awk 'NR==1 || /:5000 |:8080 |:3800 /'
for device in /dev/tty7 /dev/dri/card0; do
    code=0
    output=$(fuser "$device" 2>&1) || code=$?
    printf 'DEVICE=%s FUSER_EXIT=%s OUTPUT=%s\n' "$device" "$code" "$output"
done
for directory in "$base/runtime/data" /userdata/shared; do
    if [[ -d $directory ]]; then
        printf 'EXISTING_CONFIGURATION_DIRECTORY=%s\n' "$directory"
        find "$directory" -maxdepth 1 -type f \( -name '*.ini' -o -name '*.cfg' -o -name 'ACA_V1.0OffLineD-500' \) -printf '%f\n' | LC_ALL=C sort
    fi
done
command -v sqlite3 || true
printf 'READONLY_BOARD_A_ENVIRONMENT_COMPLETE=1\n'
