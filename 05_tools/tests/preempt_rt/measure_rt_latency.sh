set -euo pipefail
export LC_ALL=C
base=/home/rkshare/programs/rt_validation
name="$1"
case "$name" in rt_20261022_[0-9][0-9][0-9][0-9][0-9][0-9]) ;; *) exit 2 ;; esac
run="$base/$name"
test "$(uname -r)" = 6.1.99-rt36-tspi
test "$(cat /sys/kernel/realtime)" = 1
test "$(cat "$run/phase.txt")" = preflight_complete
test -z "$(pgrep -x cyclictest || true)"
test -z "$(pgrep -x stress-ng || true)"
cd "$base"
sha256sum -c tools.sha256 > "$run/tools_verify.txt"
test "$(sha256sum tools/usr/bin/cyclictest | cut -d' ' -f1)" = 62dbb170f0d461bcf69490268d0550944599898add163f40016ab70a5c642641
snapshot() {
    date -Iseconds
    uptime
    cat /proc/loadavg
    for p in /sys/devices/system/cpu/cpufreq/policy*; do
        printf '%s\n' "$p"
        cat "$p/scaling_governor" "$p/scaling_min_freq" "$p/scaling_max_freq" "$p/scaling_cur_freq"
    done
    for z in /sys/class/thermal/thermal_zone*; do printf '%s ' "$z"; cat "$z/type" "$z/temp"; done
}
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
sed -n '/^CPU_POLICY$/,/^stress-ng/{ /^stress-ng/d; p; }' normal_20261022/conditions.txt > "$run/normal_cpu_policy.txt"
sed -n '/^CPU_POLICY$/,/^stress-ng/{ /^stress-ng/d; p; }' "$run/conditions.txt" > "$run/rt_cpu_policy.txt"
cmp "$run/normal_cpu_policy.txt" "$run/rt_cpu_policy.txt"
snapshot > "$run/before_test.txt"
common=(-a0-7 -t8 -i1000 -d0 --policy=fifo -p80 -m --default-system -D60s -q -h2000)
printf 'idle\n' > "$run/phase.txt"
printf 'PHASE=idle START='; date -Iseconds
set +e
sudo -n timeout 80s "$base/tools/usr/bin/cyclictest" "${common[@]}" --json="$run/idle.json" --histfile="$run/idle.hist" > "$run/idle.txt" 2>&1
idle_exit=$?
set -e
printf '%s\n' "$idle_exit" > "$run/idle.exit"
test "$idle_exit" = 0
printf 'PHASE=idle EXIT=%s END=' "$idle_exit"; date -Iseconds
snapshot > "$run/after_idle.txt"
printf 'cpu_stress\n' > "$run/phase.txt"
stress-ng --cpu 8 --cpu-method all --timeout 70s --metrics-brief > "$run/stress.txt" 2>&1 &
stress_pid=$!
trap 'kill "$stress_pid" 2>/dev/null || true' EXIT
sleep 5
printf 'PHASE=cpu_stress START='; date -Iseconds
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
printf 'PHASE=cpu_stress EXIT=%s STRESS_EXIT=%s END=' "$rt_exit" "$stress_exit"; date -Iseconds
snapshot > "$run/after_test.txt"
sudo -n dmesg > "$run/after_test_dmesg.log"
if grep -Ei '(^|[[:space:]])(BUG:|Oops:|Kernel panic|WARNING:|Call trace:|soft lockup|hard LOCKUP|hung_task)|rcu.*stall|sleeping function called' "$run/after_test_dmesg.log" > "$run/after_test_severe.txt"; then exit 3; fi
systemctl is-active ssh smbd > "$run/services_after.txt"
printf 'complete\n' > "$run/phase.txt"
cat "$run/idle.txt" "$run/cpu_stress.txt" "$run/stress.txt"
printf 'RT_LATENCY_RUN=COMPLETE\n'
