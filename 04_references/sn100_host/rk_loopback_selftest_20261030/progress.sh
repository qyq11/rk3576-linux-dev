#!/usr/bin/env bash
set -Eeuo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
run=$(cat "$here/remote_path")
ssh -T -o BatchMode=yes -o ConnectTimeout=8 lckfb@192.168.100.123 bash -s -- "$run" <<'REMOTE'
set -Eeuo pipefail
run=$1
[[ $run =~ ^/tmp/sn100-loopback-20261030\.[a-zA-Z0-9]{6}$ ]]
date -Is
printf 'TX_EVENTS='; grep -c '^STATUS_LOOPBACK_WIRE .* TX HEX=' "$run/app.log" || true
printf 'RX_EVENTS='; grep -c '^STATUS_LOOPBACK_WIRE .* RX HEX=' "$run/app.log" || true
printf 'LOOPBACK_ERRORS='; grep -c '^STATUS_LOOPBACK_ERROR' "$run/app.log" || true
tail -n 4 "$run/app.log"
if [[ -f $run/exit_code ]]; then printf 'ACTUAL_EXIT_CODE='; cat "$run/exit_code"; fi
if [[ -f $run/product_pid ]]; then ps -p "$(cat "$run/product_pid")" -o pid,etime,pcpu,rss,stat,args || true; fi
REMOTE
