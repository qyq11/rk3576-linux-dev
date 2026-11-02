#!/usr/bin/env bash
set -euo pipefail
entry_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
project=$(realpath "$entry_dir/../..")
sdk=${RK_SYSTEM_SDK_DIR:-/home/qyq/TaishanPi-3-Linux}
patchfile=$project/04_references/preempt_rt/tspi_6.1.99_rt36_candidate.patch
kernel=$sdk/kernel-6.1
test "$(id -un)" = qyq
test "$(git -C "$kernel" rev-parse HEAD)" = bf3e03f69e6a3b766dc6356a7475e8ad64a48a98
test "$(sha256sum "$patchfile" | cut -d' ' -f1)" = a66d623a601b26fd286c9e68af855897832950a6e1168fa485803f5151c6a238
if git -C "$kernel" apply --check --reverse "$patchfile" 2>/dev/null; then
    printf 'RT_PATCH=already_applied\n'
else
    test -z "$(git -C "$kernel" diff --name-only)"
    git -C "$kernel" apply --check "$patchfile"
    git -C "$kernel" apply "$patchfile"
    git -C "$kernel" apply --check --reverse "$patchfile"
    git -C "$kernel" diff --check
    printf 'RT_PATCH=applied\n'
fi
cp "$entry_dir/configs/rk3576_image_rt.config" "$kernel/arch/arm64/configs/rk3576_image_rt.config"
printf 'RT_PREPARATION_EXIT=0\n'
