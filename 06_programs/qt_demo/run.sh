#!/usr/bin/env bash
# WSL entry point. The privileged launcher only allocates a VT; the Qt app runs as lckfb.
set -Eeuo pipefail
program_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
programs=$(cd -- "$program_dir/.." && pwd)
root=$(cd -- "$programs/.." && pwd)
host=${RK_HOST:-lckfb@192.168.100.123}
action=${1:-start}
seconds=${2:-1800}
[[ $seconds =~ ^[0-9]+$ && $seconds -ge 1 && $seconds -le 86400 ]] || { echo 'Duration must be 1..86400 seconds.' >&2; exit 2; }
opts=(-o BatchMode=yes -o ConnectTimeout=6)
case $action in
    start)
        ssh -T "${opts[@]}" "$host" 'if sudo -n systemctl is-active --quiet rk-qt-demo.service; then echo "qt_demo is already running; stop it before deploying." >&2; exit 1; fi'
        bash "$programs/rk.sh" qt_demo deploy
        ssh -T "${opts[@]}" "$host" bash -s -- "$seconds" <<'REMOTE'
set -Eeuo pipefail
base=/home/rkshare/programs/qt_demo
if sudo -n systemctl is-active --quiet rk-qt-demo.service; then
    echo 'qt_demo is already running. Use run.sh status or run.sh stop.' >&2
    exit 1
fi
test "$(cat /sys/class/drm/card0-HDMI-A-1/status)" = connected
run=$base/runs/$(date +%Y%m%d_%H%M%S)_$$
mkdir -p "$run/runtime"
chmod 700 "$run/runtime"
printf '%s\n' "$run" > "$base/latest_run"
cat > "$run/child.sh" <<'CHILD'
#!/usr/bin/env bash
set -u
run=$1
seconds=$2
cd /home/rkshare/programs/qt_demo
exec > "$run/app.log" 2>&1
export XDG_RUNTIME_DIR=$run/runtime
export QT_LOGGING_RULES='qt.qpa.eglfs.kms=true;qt.qpa.egldeviceintegration=true;qt.qpa.input=true'
./qt_demo --timeout "$seconds" --state "$run/state.json" --snapshot "$run/render.png" &
child=$!
printf '%s\n' "$child" > "$run/app.pid"
wait "$child"
code=$?
printf '%s\n' "$code" > "$run/exit_code"
exit "$code"
CHILD
sudo -n systemd-run --quiet --collect --unit=rk-qt-demo --property=Type=exec \
    --property="RuntimeMaxSec=$(($1 + 15))" \
    /usr/bin/openvt -c 7 -s -w -- /usr/sbin/runuser -u lckfb -- /bin/bash "$run/child.sh" "$run" "$1"
printf 'RUN_DIR=%s\nDURATION=%ss\n' "$run" "$1"
sleep 3
grep -E 'START |READY |Could not|failed|Failed|Error|error' "$run/app.log" 2>/dev/null | tail -15 || true
if [[ -f $run/exit_code ]]; then
    code=$(cat "$run/exit_code")
    echo "APP_EXIT=$code"
    exit "$code"
fi
test -f "$run/app.pid"
kill -0 "$(cat "$run/app.pid")"
echo 'APP_RUNNING=1'
REMOTE
        ;;
    status|stop)
        ssh -T "${opts[@]}" "$host" bash -s -- "$action" <<'REMOTE'
set -Eeuo pipefail
base=/home/rkshare/programs/qt_demo
run=$(cat "$base/latest_run")
case $run in "$base"/runs/*) ;; *) exit 2 ;; esac
if [[ $1 == stop && -f $run/app.pid && ! -f $run/exit_code ]]; then
    pid=$(cat "$run/app.pid")
    if [[ $(readlink "/proc/$pid/exe") == "$base/qt_demo" ]]; then
        kill -TERM "$pid"
        sleep 2
    fi
fi
printf 'RUN_DIR=%s\n' "$run"
if [[ -f $run/exit_code ]]; then printf 'APP_EXIT='; cat "$run/exit_code"; fi
if [[ -f $run/state.json ]]; then cat "$run/state.json"; fi
tail -20 "$run/app.log"
REMOTE
        ;;
    *) echo 'Usage: bash qt_demo/run.sh [start [SECONDS]|status|stop]' >&2; exit 2 ;;
esac
