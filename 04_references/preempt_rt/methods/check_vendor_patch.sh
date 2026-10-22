set -euo pipefail
export LC_ALL=C GIT_OPTIONAL_LOCKS=0
work=/home/qyq/rk3576/preempt_rt_vendor_reference_20261022
kernel=/home/qyq/rk3576/preempt_rt_6.1.99_rt36_20261022/kernel
cd "$kernel"
set +e
git apply -p2 --check --verbose "$work/0001-Add-PREEMPT_RT-patch-corresponding-to-kernel-6.1.99.patch" > "$work/kernel_p2_apply_check.log" 2>&1
status=$?
set -e
printf 'P2_CHECK_EXIT=%s\n' "$status"
grep -E '^error:|^warning:' "$work/kernel_p2_apply_check.log" || true
git apply -p2 --stat "$work/0001-Add-PREEMPT_RT-patch-corresponding-to-kernel-6.1.99.patch" | tail -n 8
printf 'RT_PATCH_SUMMARY\n'
grep -n -E '^diff --git|^Subject:' "$work/0001-Add-PREEMPT_RT-patch-corresponding-to-kernel-6.1.99.patch" | tail -n 18
printf 'ORIGINAL_STATUS\n'
git -C /home/qyq/rk3576/TaishanPi-3-Linux/kernel-6.1 status --short
