set -euo pipefail
export LC_ALL=C
test "$(uname -r)" = 6.1.99
base=/home/rkshare/programs/rt_validation
run="$base/normal_20261022"
test ! -e "$run"
mkdir "$run"
cd "$base"
{
    date -Iseconds
    uname -a
    cat /proc/cmdline
    printf 'CYCLICTEST_SHA256\n'
    sha256sum tools/usr/bin/cyclictest
    printf 'CPU_POLICY\n'
    for p in /sys/devices/system/cpu/cpufreq/policy*; do
        printf '%s\n' "$p"
        cat "$p/scaling_governor" "$p/scaling_min_freq" "$p/scaling_max_freq"
    done
    stress-ng --version
    printf 'CONDITIONS: eight pinned threads, 1000us period each, FIFO priority 80, mlockall, default-system, 60s per phase; stress phase CPU only. No persistent system tuning.\n'
} > "$run/conditions.txt"
common=(-a0-7 -t8 -i1000 -d0 --policy=fifo -p80 -m --default-system -D60s -q -h2000)
printf 'idle\n' > "$run/phase.txt"
set +e
sudo -n timeout 80s "$base/tools/usr/bin/cyclictest" "${common[@]}" --json="$run/idle.json" --histfile="$run/idle.hist" > "$run/idle.txt" 2>&1
idle_exit=$?
set -e
printf '%s\n' "$idle_exit" > "$run/idle.exit"
test "$idle_exit" = 0
printf 'cpu_stress\n' > "$run/phase.txt"
stress-ng --cpu 8 --cpu-method all --timeout 70s --metrics-brief > "$run/stress.txt" 2>&1 &
stress_pid=$!
trap 'kill "$stress_pid" 2>/dev/null || true' EXIT
sleep 5
set +e
sudo -n timeout 80s "$base/tools/usr/bin/cyclictest" "${common[@]}" --json="$run/cpu_stress.json" --histfile="$run/cpu_stress.hist" > "$run/cpu_stress.txt" 2>&1
rt_exit=$?
wait "$stress_pid"
stress_exit=$?
set -e
trap - EXIT
printf '%s\n' "$rt_exit" > "$run/cpu_stress.exit"
printf '%s\n' "$stress_exit" > "$run/stress.exit"
test "$rt_exit" = 0
test "$stress_exit" = 0
printf 'complete\n' > "$run/phase.txt"
cat "$run/conditions.txt" "$run/idle.txt" "$run/cpu_stress.txt" "$run/stress.txt"
printf 'BASELINE_COMPLETED=PASS\n'
