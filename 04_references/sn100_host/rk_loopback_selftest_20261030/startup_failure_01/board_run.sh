#!/usr/bin/env bash
# 正式程序在RK新匿名根循环查询本机模拟端；5分钟后正常退出，不接真实仪器。
set -Eeuo pipefail
run=$1
[[ $run =~ ^/tmp/sn100-loopback-20261030\.[a-zA-Z0-9]{6}$ && $(readlink -f "$run") == "$run" && $EUID != 0 ]]
umask 077
base=/home/rkshare/programs/sn100_shell
exec 9>"$base/lifecycle.lock"; flock -n 9
release=$(readlink -f "$base/current")
test "${release##*/}" = ef294a921e7eec2d293beb5aa0fbe5784d6b5056cef92648e799c3529b839a14
source "$release/rk_board.sh"; verify_release "$release"
test "$(sha256sum "$release/sn100_shell" | cut -d' ' -f1)" = c16b44357595b524a3cea1ed71574a6e2809b9968cea0076a86ff2f336674ef4
runtime_environment "$release" "$run/cache"
export QT_QPA_PLATFORM=eglfs XDG_RUNTIME_DIR=$run/xdg
mkdir "$run/data" "$run/xdg"; chmod 700 "$run/xdg"
cat > "$run/kms.json" <<'KMS'
{"device":"/dev/dri/card0","hwcursor":false,"outputs":[{"name":"HDMI1","mode":"preferred","primary":true},{"name":"DP1","mode":"off"},{"name":"Writeback1","mode":"off"}]}
KMS
export QT_QPA_EGLFS_KMS_CONFIG=$run/kms.json
{ date -Is; uname -a; id; getconf CLK_TCK; printf 'RELEASE=%s\nMODE=eglfs\nREAL_INSTRUMENT=0\nDATA_ROOT=%s/data\nREQUESTED_QUERY_SECONDS=300\n' "$release" "$run"; sha256sum "$release/sn100_shell" "$base/sn100.sh"; } > "$run/runtime.txt"
LD_LIBRARY_PATH="$release/lib" ldd "$release/sn100_shell" > "$run/dependencies.txt"
! grep -q 'not found' "$run/dependencies.txt"
cd "$release"
args=(--offline --accounts --history --catalog --samples --instrument --status-loopback --data-dir "$run/data" --viewport 1600x900)
printf '%s\n' "${args[@]}" > "$run/arguments.txt"
"$release/sn100_shell" "${args[@]}" > "$run/app.log" 2>&1 &
child=$!; printf '%s\n' "$child" > "$run/product_pid"
cleanup_child() {
    if kill -0 "$child" 2>/dev/null; then kill -TERM "$child" 2>/dev/null || true; fi
}
trap cleanup_child EXIT
printf 'uptime_s\tuser_ticks\tsystem_ticks\trss_kib\tpss_kib\tthreads\tfd_count\ttemperatures_mC\tfrequencies_kHz\n' > "$run/resources.tsv"
sample() {
    [[ -r /proc/$child/stat ]] || return 0
    local stat rest rss pss threads fds uptime ignored temps= freqs= node
    stat=$(cat "/proc/$child/stat" 2>/dev/null || true); rest=${stat##*) }; set -- $rest
    rss=$(awk '$1=="VmRSS:" {print $2}' "/proc/$child/status" 2>/dev/null || true)
    pss=$(awk '$1=="Pss:" {print $2}' "/proc/$child/smaps_rollup" 2>/dev/null || true)
    threads=$(awk '$1=="Threads:" {print $2}' "/proc/$child/status" 2>/dev/null || true)
    fds=$(find "/proc/$child/fd" -maxdepth 1 -type l 2>/dev/null | wc -l || true)
    read -r uptime ignored < /proc/uptime
    for node in /sys/class/thermal/thermal_zone*/temp; do temps+="$(cat "$node");"; done
    for node in /sys/devices/system/cpu/cpufreq/policy*/scaling_cur_freq; do freqs+="$(cat "$node");"; done
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$uptime" "${12:-NA}" "${13:-NA}" "${rss:-NA}" "${pss:-NA}" "${threads:-NA}" "$fds" "$temps" "$freqs" >> "$run/resources.tsv"
}
ready=0
for attempt in $(seq 1 100); do
    if grep -q '^STATUS_LOOPBACK_STARTED ' "$run/app.log"; then ready=1; break; fi
    kill -0 "$child" 2>/dev/null || break
    sleep .2
done
printf 'STARTUP_READY=%s\n' "$ready" >> "$run/runtime.txt"
test "$ready" = 1
start=$SECONDS; deadline=$((SECONDS+300))
printf '%s\n' "$start" > "$run/query_start_seconds"
while (( SECONDS < deadline )); do
    kill -0 "$child" 2>/dev/null || break
    sample
    if grep -q '^STATUS_LOOPBACK_ERROR' "$run/app.log"; then break; fi
    sleep 1
done
elapsed=$((SECONDS-start)); printf 'OBSERVED_QUERY_SECONDS=%s\n' "$elapsed" >> "$run/runtime.txt"
ss -tnp | grep "pid=$child," > "$run/product_sockets.txt" || true
kill -TERM "$child" 2>/dev/null || true
forced=0
for attempt in $(seq 1 100); do
    kill -0 "$child" 2>/dev/null || break
    sleep .1
done
if kill -0 "$child" 2>/dev/null; then forced=1; kill -KILL "$child"; fi
set +e; wait "$child"; code=$?; set -e
trap - EXIT
printf '%s\n' "$code" > "$run/exit_code"
printf 'ACTUAL_EXIT_CODE=%s\nFORCED_KILL=%s\n' "$code" "$forced" >> "$run/runtime.txt"
du -sb "$run/data" "$run/cache" "$run/app.log" >> "$run/runtime.txt"
test "$(readlink -f "$base/current")" = "$release"
sha256sum "$release/sn100_shell" "$base/sn100.sh" >> "$run/runtime.txt"
printf 'CURRENT_UNCHANGED=1\n' >> "$run/runtime.txt"
[[ $code == 0 && $forced == 0 && $elapsed -ge 300 ]]
grep -q 'STATUS_LOOPBACK_STOPPED true' "$run/app.log"
grep -q 'THREADS_STOPPED true' "$run/app.log"
! grep -q '^STATUS_LOOPBACK_ERROR' "$run/app.log"
printf 'RUNNER_CHECKS_PASSED=1\n' >> "$run/runtime.txt"
