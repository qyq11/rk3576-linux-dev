#!/usr/bin/env bash
set -uo pipefail
work=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$work/../../.." && pwd)
exec > >(tee "$work/manual_control.log") 2>&1
expected=$(sha256sum "$root/06_programs/sn100_host/rk_control.sh" | cut -d' ' -f1)
actual=$(ssh -T -o BatchMode=yes -o ConnectTimeout=5 lckfb@192.168.100.123 'sha256sum /home/rkshare/programs/sn100_shell/sn100.sh' | cut -d' ' -f1)
[[ $actual == "$expected" ]] || exit 1
printf 'CONTROL_SCRIPT_SHA256=%s\n' "$actual"
for action in start status stop status; do
    ssh -T -o BatchMode=yes -o ConnectTimeout=6 lckfb@192.168.100.123 "bash /home/rkshare/programs/sn100_shell/sn100.sh $action"
    code=$?
    printf 'CONTROL_%s_EXIT=%s\n' "$action" "$code"
    [[ $code == 0 ]] || exit "$code"
done
ssh -T -o BatchMode=yes -o ConnectTimeout=5 lckfb@192.168.100.123 'if pgrep -x sn100_shell >/dev/null; then printf "UNEXPECTED_RUNNING_APP=1\n"; exit 1; fi; printf "PROGRAM_LEFT_STOPPED=1\n"; if systemctl is-enabled sn100-shell.service >/dev/null 2>&1; then printf "AUTOSTART_ENABLED=1\n"; exit 1; fi; printf "NO_AUTOSTART=1\n"'
code=$?
printf '%s\n' "$code" > "$work/manual_control.exit"
exit "$code"
