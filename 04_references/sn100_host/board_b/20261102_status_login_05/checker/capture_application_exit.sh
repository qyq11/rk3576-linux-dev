#!/usr/bin/env bash
# 由限时封装调用，单独保存实际子进程wait码。这里不主动发信号，也不操作主控。
set -uo pipefail
binary=$1; result=$2; shift 2
interrupted=0
# timeout会向本组发送TERM；保留等待者以取回应用实际wait码，不补发第二次信号。
trap 'interrupted=1' TERM INT HUP
"$binary" "$@" &
child=$!
while :; do
    interrupted=0
    wait "$child"
    code=$?
    if [[ $interrupted == 1 ]]; then continue; fi
    break
done
printf '%s\n' "$code" >"$result"
exit "$code"
