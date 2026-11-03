#!/usr/bin/env bash
# 在板B检查正式程序的后台初始化、匿名数据重开和正常退出；画面另接屏核对。
set -Eeuo pipefail
umask 077
base=/home/rkshare/programs/sn100_shell
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
test "$(id -un)" = lckfb
exec 9>"$base/lifecycle.lock"
flock -n 9
release=$(readlink -f "$base/current")
source "$release/rk_board.sh"
verify_release "$release"
run=$base/verification/board_b_20261102_offscreen
test ! -e "$run"
mkdir -p "$run/xdg" "$run/data"
chmod 700 "$run/xdg"
runtime_environment "$release" "$run/cache"
export QT_QPA_PLATFORM=offscreen XDG_RUNTIME_DIR=$run/xdg
unset QT_QPA_EGLFS_INTEGRATION
{
    printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\n'
    uname -a
    id
    printf 'QPA=offscreen\nREAL_INSTRUMENT=0\nRELEASE=%s\n' "$release"
    printf 'CJK_FONT='
    fc-match -f '%{family}\n' 'Noto Sans CJK SC:lang=zh-cn'
    printf 'HDMI='
    cat /sys/class/drm/card0-HDMI-A-1/status
} > "$run/environment.txt"
cat /proc/sys/kernel/random/uuid > "$run/bootstrap_secret"
trap 'rm -f -- "$run/bootstrap_secret"' EXIT
cd "$release"
for cycle in 1 2; do
    seed=()
    if [[ $cycle == 1 ]]; then seed=(--seed-anonymous --test-secret-file "$run/bootstrap_secret" --seed-catalog-demo); fi
    "$release/sn100_shell" --offline --accounts --password-login --history --catalog --samples --instrument \
        --printer-settings --lis-settings --quality-control --calibration --alarm-history \
        --test-session offlineAdmin --data-dir "$run/data" --viewport 1600x900 "${seed[@]}" > "$run/app_$cycle.log" 2>&1 &
    child=$!
    ready=0
    for ((attempt=0; attempt<150; attempt++)); do
        if grep -qx SHELL_READY "$run/app_$cycle.log"; then ready=1; break; fi
        kill -0 "$child" 2>/dev/null || break
        sleep .2
    done
    printf 'cycle\tuptime\trss_kib\tthreads\tfd_count\n' > "$run/resources_$cycle.tsv"
    if [[ $ready == 1 ]]; then
        for ((sample=0; sample<10; sample++)); do
            read -r uptime _ < /proc/uptime
            rss=$(awk '$1=="VmRSS:" {print $2}' "/proc/$child/status")
            threads=$(awk '$1=="Threads:" {print $2}' "/proc/$child/status")
            fds=$(find "/proc/$child/fd" -mindepth 1 -maxdepth 1 -type l | wc -l)
            printf '%s\t%s\t%s\t%s\t%s\n' "$cycle" "$uptime" "$rss" "$threads" "$fds" >> "$run/resources_$cycle.tsv"
            sleep 1
        done
    fi
    kill -TERM "$child" 2>/dev/null || true
    set +e
    wait "$child"
    code=$?
    set -e
    printf '%s\n' "$code" > "$run/exit_$cycle.txt"
    printf 'CYCLE=%s READY=%s EXIT=%s\n' "$cycle" "$ready" "$code" | tee -a "$run/result.txt"
    test "$ready" = 1 && test "$code" = 0
    grep -qx 'THREADS_STOPPED true' "$run/app_$cycle.log"
    for marker in HOSPITAL_STORE_READY ACCOUNT_STORE_READY HISTORY_STORE_READY PROJECTS_STORE_READY SAMPLE_STORE_READY QUALITY_CONTROL_READY CALIBRATION_READY; do
        if grep -q "$marker" "$run/app_$cycle.log"; then printf 'CYCLE=%s MARKER=%s\n' "$cycle" "$marker"; fi
    done
done
printf 'TWO_STARTS_AND_CLEAN_EXITS=1\nREAL_INSTRUMENT=0\nPHYSICAL_DISPLAY_VERIFIED=0\n' | tee -a "$run/result.txt"
