#!/usr/bin/env bash
# 仅在已确认的板B新建应用目录，检查正式包，不覆盖已有应用或数据。
set -Eeuo pipefail
umask 077
expected=ef294a921e7eec2d293beb5aa0fbe5784d6b5056cef92648e799c3529b839a14
base=/home/rkshare/programs/sn100_shell
upload=/home/rkshare/programs/sn100_board_b_20261102.tar.gz
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
test "$(id -un)" = lckfb
test "$(uname -m)" = aarch64
test ! -e "$base" && test ! -L "$base"
test "$(sha256sum "$upload" | cut -d' ' -f1)" = d37170cced425523b45abbeddc24cafe577bd55bf83f14c06ee192845e73bd3e
! systemctl is-active --quiet sn100-shell.service
mkdir -m 700 "$base"
mkdir -m 700 "$base/releases" "$base/deploy_checks"
release=$base/releases/$expected
mkdir -m 700 "$release"
tar --no-same-owner --no-same-permissions -xzf "$upload" -C "$release"
test "$(cat "$release/release_id")" = "$expected"
test "$(sha256sum "$release/SHA256SUMS" | cut -d' ' -f1)" = "$expected"
(cd "$release" && sha256sum -c SHA256SUMS)
chmod 700 "$release/sn100_shell" "$release/sn100_report_renderer"
source "$release/rk_board.sh"
verify_release "$release"
test "$(od -An -tu2 -j18 -N2 "$release/sn100_shell" | tr -d ' ')" = 183
test "$(sha256sum "$release/sn100_shell" | cut -d' ' -f1)" = c16b44357595b524a3cea1ed71574a6e2809b9968cea0076a86ff2f336674ef4
export LD_LIBRARY_PATH=$release/lib
objects=("$release/sn100_shell" "$release/sn100_report_renderer")
while IFS= read -r -d '' plugin; do objects+=("$plugin"); done < <(find "$release/plugins" -type f -name '*.so' -print0)
objects+=(/usr/lib/aarch64-linux-gnu/qt5/plugins/platforms/libqeglfs.so
 /usr/lib/aarch64-linux-gnu/qt5/plugins/egldeviceintegrations/libqeglfs-kms-integration.so)
for object in "${objects[@]}"; do
    printf '[object] %s\n' "$object"
    ldd "$object"
done | tee "$base/deploy_checks/dependencies.txt"
! grep -q 'not found' "$base/deploy_checks/dependencies.txt"
ln -s "releases/$expected" "$base/current"
printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\nDEPLOYED_RELEASE=%s\nDEPENDENCIES_OK=1\nSYSTEM_PACKAGES_CHANGED=0\n' "$expected"
