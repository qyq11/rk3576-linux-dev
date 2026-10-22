set -euo pipefail
export LC_ALL=C GIT_OPTIONAL_LOCKS=0
sdk=/home/qyq/rk3576/TaishanPi-3-Linux
work=/home/qyq/rk3576/preempt_rt_6.1.99_rt36_20261022
cross="$sdk/prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-"
test ! -e "$work/build.exit"
printf 'BUILD_STARTED=%s\n' "$(date -Iseconds)" > "$work/build_state.txt"
set +e
make -C "$work/kernel" O="$work/build" ARCH=arm64 CROSS_COMPILE="$cross" \
    -j12 Image modules rockchip/tspi-3m-rk3576-panfrost.dtb > "$work/build.log" 2>&1
result=$?
set -e
printf '%s\n' "$result" > "$work/build.exit"
printf 'BUILD_FINISHED=%s\nBUILD_EXIT=%s\n' "$(date -Iseconds)" "$result" >> "$work/build_state.txt"
tail -n 55 "$work/build.log"
printf 'BUILD_EXIT=%s\n' "$result"
exit "$result"
