#!/usr/bin/env bash
set -Eeuo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
run=$(cat "$here/$1/remote_path")
[[ $run =~ ^/tmp/sn100-calendar-20261030\.[a-zA-Z0-9]{6}$ ]]
ssh -o BatchMode=yes -o ConnectTimeout=8 lckfb@192.168.100.123 bash -s -- "$run" <<'REMOTE'
ls -l "$1/app.log" || true
cat "$1/exit_code" 2>/dev/null || true
tail -n 12 "$1/app.log" 2>/dev/null || true
tail -n 3 "$1/resources.tsv" 2>/dev/null || true
ps -C driver -o pid,etime,time,pcpu,rss,stat,args || true
ls -la "$1" | tail -10
ps -eo pid,ppid,etime,comm | tail -12
if [[ -f $1/exit_code ]]; then cat "$1/exit_code"; fi
REMOTE
