set -euo pipefail
export LC_ALL=C
release=6.1.99-rt36-tspi
base=/home/rkshare/programs/rt_validation
name="$1"
expected_config="$2"
case "$name" in rt_20261022_[0-9][0-9][0-9][0-9][0-9][0-9]) ;; *) exit 2 ;; esac
run="$base/$name"
test "$(uname -r)" = "$release"
test "$(cat /sys/kernel/realtime)" = 1
test ! -e "$run"
mkdir "$run"
printf 'preflight\n' > "$run/phase.txt"
gzip -dc /proc/config.gz > "$run/board_kernel.config"
printf '%s  %s\n' "$expected_config" "$run/board_kernel.config" | sha256sum -c - > "$run/config_verify.txt"
grep -Fx 'CONFIG_PREEMPT_RT=y' "$run/board_kernel.config"
grep -Fx 'CONFIG_HIGH_RES_TIMERS=y' "$run/board_kernel.config"
test "$(readlink -f /dev/disk/by-partlabel/boot)" = /dev/mmcblk0p3
sudo -n head -c 45330944 /dev/mmcblk0p3 | sha256sum > "$run/boot_payload.sha256"
test "$(cut -d' ' -f1 "$run/boot_payload.sha256")" = 73ea7f9e89e2b0db05974b53634349af03455fd0bd244646f508a62f454f1696
(cd / && sha256sum -c "$base/rt_$release/module_files.sha256") > "$run/module_verify.txt"
test "$(find "/lib/modules/$release" -type f -name '*.ko' | wc -l)" = 155
sudo -n sha256sum -c "$base/rt_$release/old_files_before.sha256" > "$run/old_files_verify.txt"
{
    date -Iseconds
    id
    uname -a
    printf 'REALTIME='; cat /sys/kernel/realtime
    printf 'BOOT_ID='; cat /proc/sys/kernel/random/boot_id
    uptime
    cat /proc/cmdline
    printf 'CONFIG_SHA256\n'; sha256sum "$run/board_kernel.config"
    printf 'ROOT_MOUNT\n'; findmnt /
    printf 'NETWORK\n'; ip -br -4 address
    printf 'SERVICES\n'; systemctl is-active ssh smbd
    printf 'FAILED_UNITS\n'; systemctl --failed --no-legend --no-pager
    printf 'NODES\n'; ls -l /dev/ttyS4 /dev/gpiochip* /dev/dri/*
    printf 'MODULES\n'; lsmod
    printf 'LOADED_MODULE_PATHS\n'
    while read -r module rest; do modinfo -F filename "$module"; modinfo -F vermagic "$module"; done < /proc/modules
    printf 'SYSCTL\n'; sysctl kernel.sched_rt_runtime_us kernel.sched_rt_period_us kernel.printk
    printf 'KERNEL_TAINT='; cat /proc/sys/kernel/tainted
} > "$run/startup.txt" 2>&1
sudo -n dmesg > "$run/startup_dmesg.log"
sudo -n dmesg --level=emerg,alert,crit,err,warn > "$run/startup_warnings.log"
if grep -Ei '(^|[[:space:]])(BUG:|Oops:|Kernel panic|WARNING:|Call trace:|soft lockup|hard LOCKUP|hung_task)|rcu.*stall|sleeping function called' "$run/startup_dmesg.log" > "$run/kernel_severe.txt"; then
    cat "$run/kernel_severe.txt"
    exit 3
fi
test -z "$(pgrep -x cyclictest || true)"
test -z "$(pgrep -x stress-ng || true)"
printf 'preflight_complete\n' > "$run/phase.txt"
cat "$run/startup.txt"
printf 'BOOT_PAYLOAD_SHA256='; cut -d' ' -f1 "$run/boot_payload.sha256"
printf 'MODULE_COUNT=155\nMODULE_FILES=PASS\nOLD_MODULES_BOOT_OVERLAYS_AND_SELECTED_CONFIGS=PASS\nPREFLIGHT=PASS\nRUN_DIRECTORY=%s\n' "$run"
