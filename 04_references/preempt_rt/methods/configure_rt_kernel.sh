set -euo pipefail
export LC_ALL=C GIT_OPTIONAL_LOCKS=0
sdk=/home/qyq/rk3576/TaishanPi-3-Linux
work=/home/qyq/rk3576/preempt_rt_6.1.99_rt36_20261022
src="$work/kernel"
out="$work/build"
cross="$sdk/prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-"
test ! -e "$out"
mkdir "$out"
cp "$sdk/kernel-6.1/.config" "$out/.config"
"$src/scripts/config" --file "$out/.config" --enable EXPERT --enable PREEMPT_RT \
    --disable PREEMPT_VOLUNTARY --disable PREEMPT_NONE --disable PREEMPT \
    --disable PREEMPT_DYNAMIC --set-str LOCALVERSION '-tspi' --disable LOCALVERSION_AUTO
make -C "$src" O="$out" ARCH=arm64 CROSS_COMPILE="$cross" olddefconfig
grep -E '^CONFIG_(EXPERT|PREEMPT|HZ|HIGH_RES_TIMERS|LOCALVERSION|IKCONFIG)|^# CONFIG_(PREEMPT|LOCALVERSION_AUTO|DEBUG_INFO_BTF)' "$out/.config"
make -s -C "$src" O="$out" ARCH=arm64 CROSS_COMPILE="$cross" kernelrelease | tee "$work/kernelrelease.txt"
test "$(cat "$work/kernelrelease.txt")" = 6.1.99-rt36-tspi
"$src/scripts/diffconfig" "$sdk/kernel-6.1/.config" "$out/.config" | tee "$work/config_changes.txt"
sha256sum "$out/.config"
test "$(grep '^CONFIG_PREEMPT_RT=' "$out/.config")" = CONFIG_PREEMPT_RT=y
test "$(grep '^CONFIG_ARCH_ROCKCHIP=' "$out/.config")" = CONFIG_ARCH_ROCKCHIP=y
test "$(grep '^CONFIG_NO_GKI=' "$out/.config")" = CONFIG_NO_GKI=y
printf 'RT_CONFIG_READY=PASS\n'
