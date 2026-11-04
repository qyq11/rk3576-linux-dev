#!/usr/bin/env bash
[[ $(cat /proc/sys/kernel/random/boot_id) == 559493ce-5319-42b6-9383-bf7b93764a35 ]]
run=/home/rkshare/programs/sn100_shell/runs/20261104_153722_5015
printf 'WINDOW_INPUT_LOG_MESSAGES\n'
grep -Ei 'eglfs|qpa\.|popup|multiple windows|window.*(error|fail|cannot)|input.*(error|fail|cannot)|evdev|keyboard|mouse' "$run/app.log" | tail -35 || true
printf 'APP_OPEN_INPUT_DEVICES\n'
for descriptor in /proc/5836/fd/[0-9]*;do
    destination=$(readlink "$descriptor" 2>/dev/null) || continue
    case "$destination" in /dev/input/*|/dev/dri/*|/dev/tty*)printf '%s %s\n' "${descriptor##*/}" "$destination";;esac
done
printf 'CONNECTED_INPUT_NAMES\n'
for path in /sys/class/input/event*/device/name;do printf '%s ' "${path%/device/name}";cat "$path";done
printf 'QT_INPUT_ENVIRONMENT_ONLY\n'
tr '\0' '\n' </proc/5836/environ | awk '/^(QT_QPA_PLATFORM|QT_QPA_EGLFS_DISABLE_INPUT|QT_QPA_EVDEV_KEYBOARD_PARAMETERS|QT_QPA_EVDEV_MOUSE_PARAMETERS|QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS|QT_QPA_GENERIC_PLUGINS)=/'
printf 'GUI_MAIN_KERNEL_STACK\n';cat /proc/5836/stack || true
printf 'DEBUGGER_AVAILABILITY\n';command -v gdb || true
printf 'QT_WIDGETS_EXPORTED_INPUT_STATE\n'
if command -v nm >/dev/null;then nm -D -C /lib/aarch64-linux-gnu/libQt5Widgets.so.5 2>/dev/null | grep -E '(popupWidgets|mouseGrabber|keyboardGrabber|active_window|modalState)' | head -12 || true;fi
printf 'READONLY_INPUT_WINDOW_CHECK_COMPLETE=1\n'
