#!/usr/bin/env bash
set -Eeuo pipefail
program_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$program_dir/../.." && pwd)
host=${RK_HOST:-lckfb@192.168.100.123}
opts=(-o BatchMode=yes -o ConnectTimeout=6)
run=$(ssh -T "${opts[@]}" "$host" 'cat /home/rkshare/programs/qt_demo/latest_run')
[[ $run =~ ^/home/rkshare/programs/qt_demo/runs/[0-9_]+$ ]] || { echo 'Unexpected run directory' >&2; exit 1; }
out=$root/temp/qt_demo/evidence/${run##*/}
mkdir -p "$out"
for name in app.log state.json render.png; do
    scp -B "${opts[@]}" "$host:$run/$name" "$out/"
done
ssh -T "${opts[@]}" "$host" bash -s -- "$run" > "$out/runtime.txt" <<'REMOTE'
set -Eeuo pipefail
uname -a
printf 'PREEMPT_RT='; cat /sys/kernel/realtime
printf 'ACTIVE_VT='; cat /sys/class/tty/tty0/active
grep -H . /sys/class/drm/card0-HDMI-A-1/status /sys/class/drm/card0-HDMI-A-1/modes
id
if [[ -f $1/exit_code ]]; then
    printf 'APP_EXIT='; cat "$1/exit_code"
else
    pid=$(cat "$1/app.pid")
    ps -p "$pid" -o pid,user,stat,etime,args
fi
sudo -n dmesg --level=emerg,alert,crit,err | tail -20
REMOTE
if ssh -T "${opts[@]}" "$host" test -f "$run/exit_code"; then
    scp -B "${opts[@]}" "$host:$run/exit_code" "$out/"
fi
printf 'LOCAL_EVIDENCE=%s\n' "$out"
cat "$out/state.json"
grep -E 'START |READY |COUNTER |RESET |SUBMIT |EXIT |MOUSE_PRESS|KEY_PRESS' "$out/app.log" | tail -30
