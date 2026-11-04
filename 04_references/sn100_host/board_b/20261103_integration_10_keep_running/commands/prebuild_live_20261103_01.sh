set -Eeuo pipefail
base=/home/rkshare/programs/sn100_shell
printf 'BOARD=board_b\nSERIAL='; tr -d '\0\n' </proc/device-tree/serial-number; printf '\n'
printf 'UTC='; date -u +%Y-%m-%dT%H:%M:%SZ
printf 'BOOT='; cat /proc/sys/kernel/random/boot_id
printf 'CURRENT='; readlink "$base/current"
printf 'CURRENT_RESOLVED='; readlink -f "$base/current"
for link in /proc/[0-9]*/exe; do
    actual=$(readlink "$link" 2>/dev/null || true)
    case "${actual##*/}" in
        sn100_shell*|sn100_report_renderer*|ACA_V1.0OffLineD-500*|rkaiq_3A_server*)
            pid=${link#/proc/}; pid=${pid%/exe}
            printf 'PROCESS pid=%s exe=%s\n' "$pid" "$actual"
            printf 'CWD='; readlink "/proc/$pid/cwd" || true
            ps -p "$pid" -o pid=,stat=,lstart=,etimes=,rss=,comm= || true
            sha256sum "/proc/$pid/exe" || true
            ;;
    esac
done
printf 'LISTEN_5000_8080\n'
ss -lntp '( sport = :5000 or sport = :8080 )'
systemctl show sn100-original-controller-board-b.service opto-camera.service \
    -p Id -p ActiveState -p SubState -p MainPID -p MemoryCurrent -p Nice
ini="$base/verification/status_login_08_permission_20261103/runs/auto_permission_01/ini_before.sha256"
controller_ini="$base/verification/status_login_08_permission_20261103/runs/auto_permission_01/controller_ini_before.sha256"
printf '%s  %s\n' 6c961ce4eff4981afa1e7b0e59711fb90eee8b45395e83e2b4049bff6fd58c38 "$ini" | sha256sum -c -
printf '%s  %s\n' d82b216b3aee25fb0298565b73a62d7144ede9a1aae1d6840bce6da8ace3b112 "$controller_ini" | sha256sum -c -
sha256sum -c "$ini"
sha256sum -c "$controller_ini"
test -f "$base/runtime/data/.sn100-original-work-v1"
printf 'ORIGINAL_DATA_MARKER=present\nREAD_ONLY_PROBE_FINISHED=1\n'
