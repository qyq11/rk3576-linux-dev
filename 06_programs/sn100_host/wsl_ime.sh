#!/usr/bin/env bash
# WSL GUI launch adapter: connect Qt5 to the user's Fcitx5 session.
set -Eeuo pipefail
[[ $# -gt 0 && $EUID != 0 ]] || { echo 'Usage (ordinary WSL user): bash wsl_ime.sh PROGRAM [arguments]' >&2; exit 2; }
if [[ ${SN100_WSL_IME:-on} == off ]]; then exec "$@"; fi
if ! command -v fcitx5 >/dev/null || ! command -v fcitx5-remote >/dev/null; then
    echo 'Chinese Pinyin requires fcitx5, fcitx5-pinyin and fcitx5-frontend-qt5 in Ubuntu; starting without it.' >&2
    exec "$@"
fi
if [[ -z ${DBUS_SESSION_BUS_ADDRESS:-} ]]; then
    echo 'No user D-Bus session is available for the Chinese input method.' >&2
    exit 1
fi
program_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
work=$(cd -- "$program_dir/../.." && pwd)/temp/sn100_host
mkdir -p "$work/logs"
# Serialize only daemon startup. Do not replace another application's daemon.
exec 9>"${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/sn100-fcitx5.lock"
if ! flock -w 5 9; then
    echo 'The Chinese input method is still starting; please try opening the software again.' >&2
    exit 1
fi
if ! fcitx5-remote --check >/dev/null 2>&1; then
    ime_options=(-d -u classicui)
    # WSLg rejects Wayland input_method binding and terminates Fcitx5.
    # Keep its user daemon on X11/D-Bus, matching the Qt5 XCB application.
    if [[ -d /mnt/wslg ]]; then ime_options+=(--disable=wayland); fi
    nohup fcitx5 "${ime_options[@]}" >> "$work/logs/fcitx5.log" 2>&1 < /dev/null 9>&-
    for attempt in {1..50}; do
        if fcitx5-remote --check >/dev/null 2>&1; then break; fi
        sleep 0.1
    done
fi
flock -u 9
exec 9>&-
if ! fcitx5-remote --check >/dev/null 2>&1; then
    echo "Chinese input method did not start; see $work/logs/fcitx5.log" >&2
    exit 1
fi
# Keep these settings local to the launched GUI, not the system or ARM builds.
export QT_IM_MODULE=fcitx XMODIFIERS=@im=fcitx
exec "$@"
