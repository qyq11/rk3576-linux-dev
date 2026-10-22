set -euo pipefail
export LC_ALL=C GIT_OPTIONAL_LOCKS=0
original=/home/qyq/rk3576/TaishanPi-3-Linux/kernel-6.1
kernel=/home/qyq/rk3576/preempt_rt_6.1.99_rt36_20261022/kernel
patchfile=/home/qyq/rk3576/preempt_rt_vendor_reference_20261022/tspi_6.1.99_rt36_candidate.patch
cd "$kernel"
test "$(git rev-parse HEAD)" = bf3e03f69e6a3b766dc6356a7475e8ad64a48a98
test -z "$(git status --porcelain)"
sha256sum "$patchfile"
git apply --check "$patchfile"
printf 'CANDIDATE_APPLY_CHECK=PASS\n'
git apply "$patchfile"
git apply --check --reverse "$patchfile"
git diff --check
test -z "$(git diff --name-only -- arch/arm64/boot/dts drivers/gpu/drm/panel drivers/input/touchscreen drivers/misc)"
grep -n -A 12 'static inline bool serial8250_set_THRI' drivers/tty/serial/8250/8250.h
grep -n -A 10 'add_taint(TAINT_SOFTLOCKUP' kernel/watchdog.c
printf 'PATCH_STAT\n'
git diff --stat | tail -n 1
cat localversion-rt
printf 'ORIGINAL_STATUS\n'
git -C "$original" status --short
sha256sum "$original/.config" "$original/boot.img"
printf 'CANDIDATE_APPLIED_TO_INDEPENDENT_COPY_ONLY=PASS\n'
