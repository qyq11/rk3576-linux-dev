#!/usr/bin/env bash
set -euo pipefail
entry_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
project=$(realpath "$entry_dir/../..")
sdk=${RK_SYSTEM_SDK_DIR:-/home/qyq/TaishanPi-3-Linux}
rootfs=$sdk/ubuntu/binary
report_dir=${RK_SYSTEM_EVIDENCE_DIR:-$project/temp/system_image_checks}
[[ $report_dir == /* ]] || { printf '检查报告目录必须是绝对路径。\n' >&2; exit 2; }
mkdir -p "$report_dir"
report=$report_dir/rootfs_content_verification.json
test "${WSL_DISTRO_NAME:-}" = RK3576-Build-22.04
test "$(id -un)" = qyq
test -f "$rootfs/etc/os-release"
test -f "$rootfs/usr/share/rk3576-image/environment.txt"

repack=false
if ! sudo cmp -s "$entry_dir/rootfs/ssh-keygen-firstboot.service" "$rootfs/etc/systemd/system/ssh-keygen-firstboot.service" \
    || ! sudo test -L "$rootfs/etc/systemd/system/ssh.service.requires/ssh-keygen-firstboot.service"; then
    sudo install -m 0644 "$entry_dir/rootfs/ssh-keygen-firstboot.service" "$rootfs/etc/systemd/system/ssh-keygen-firstboot.service"
    sudo systemctl --root="$rootfs" enable ssh-keygen-firstboot.service
    repack=true
fi
expected_uid=$(awk -F: '$1 == "lckfb" { print $3 }' "$rootfs/etc/passwd")
test -n "$expected_uid"
for directory in home/rkshare home/rkshare/lckfb home/rkshare/programs; do
    if [[ $(sudo stat -c %u "$rootfs/$directory") != "$expected_uid" ]]; then
        repack=true
    fi
done
if $repack; then
    cd "$sdk/ubuntu"
    ./mk-image.sh
    cd "$sdk"
    ./build.sh firmware
    if ! grep -q '^RK_UPDATE=y' output/.config; then
        ./build.sh updateimg
    fi
fi
sudo python3 "$entry_dir/verify_rootfs.py" "$rootfs" "$report"
test -s "$sdk/rockdev/update.img"
printf 'IMAGE_FINALIZATION_EXIT=0\n'
