#!/usr/bin/env bash
set -u
run=$1
[[ $run == /home/rkshare/programs/sn100_original/20261027_01/verification/page_* && $EUID != 0 ]] || exit 2
release=$(readlink -f /home/rkshare/programs/sn100_shell/current)
[[ $(sha256sum "$release/SHA256SUMS" | cut -d' ' -f1) == "${release##*/}" ]] || exit 3
(cd "$release" && sha256sum --quiet -c SHA256SUMS) || exit 3
source "$release/rk_board.sh"
runtime_environment "$release" "$run/cache"
export LD_LIBRARY_PATH="$run/lib:$LD_LIBRARY_PATH"
mkdir -p "$run/xdg"; chmod 700 "$run/xdg"
cat > "$run/kms.json" <<'KMS'
{"device":"/dev/dri/card0","hwcursor":false,"outputs":[{"name":"HDMI1","mode":"preferred","primary":true},{"name":"DP1","mode":"off"},{"name":"Writeback1","mode":"off"}]}
KMS
export XDG_RUNTIME_DIR=$run/xdg QT_QPA_EGLFS_KMS_CONFIG=$run/kms.json
cd "$run" || exit 2
printf 'ARCH=%s\nKERNEL=%s\nQPA=%s\nDATA_ROOT=%s\n' "$(uname -m)" "$(uname -r)" "$QT_QPA_PLATFORM" "$run/data" > runtime.txt
set +e
SN100_BUTTON_TEST_ROOT="$run/data" ./driver -v1 > app.log 2>&1 &
child=$!
printf '%s\n' "$child" > app.pid
printf 'uptime_s\tutime_ticks\tstime_ticks\trss_kib\tpss_kib\tthreads\tfd_count\ttemperatures_mC\n' > resources.tsv
sample()
{
    local stat rest rss pss threads fds uptime temperatures= node
    [[ -r /proc/$child/status ]] || return
    stat=$(cat "/proc/$child/stat") || return
    rest=${stat##*) }; set -- $rest
    rss=$(awk '$1=="VmRSS:" {print $2}' "/proc/$child/status")
    pss=$(awk '$1=="Pss:" {print $2}' "/proc/$child/smaps_rollup" 2>/dev/null)
    threads=$(awk '$1=="Threads:" {print $2}' "/proc/$child/status")
    fds=$(find "/proc/$child/fd" -maxdepth 1 -type l 2>/dev/null | wc -l)
    read -r uptime _ < /proc/uptime
    for node in /sys/class/thermal/thermal_zone*/temp; do temperatures+="$(cat "$node");"; done
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$uptime" "${12:-unavailable}" "${13:-unavailable}" "${rss:-unavailable}" "${pss:-unavailable}" "${threads:-unavailable}" "$fds" "$temperatures" >> resources.tsv
}
start=$SECONDS
while kill -0 "$child" 2>/dev/null; do
    sample
    if (( SECONDS - start > 150 )); then kill -TERM "$child"; fi
    sleep 0.2
done
wait "$child"
code=$?
printf '%s\n' "$code" > exit_code
printf 'ACTUAL_EXIT_CODE=%s\n' "$code" > result.txt
exit "$code"
