#!/usr/bin/env bash
# 仅暂存第07批；任何失败都保留现场，不切current，不启停程序。
set -Eeuo pipefail
test "$(id -u)" = 0
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
test "$#" = 3
archive=$1
expected=$2
release_id=$3
base=/home/rkshare/programs/sn100_shell
baseline=releases/14ce9714aaa0017c7ad78ff3f551d978672e5def79b96e817605295d7e59020c
[[ $archive =~ ^/home/rkshare/programs/sn100_shell/\.workflow_upload_07_[a-f0-9]{32}\.tar\.gz$ ]]
[[ $expected =~ ^[a-f0-9]{64}$ && $release_id =~ ^[a-f0-9]{64}$ ]]
test "$(readlink -f "$base")" = "$base"
test -d "$base/releases"
test ! -L "$base/releases"
test ! -L "$base/lifecycle.lock"
test -L "$base/current"
test "$(readlink "$base/current")" = "$baseline"
exec 9>>"$base/lifecycle.lock"
flock -n 9
# 取得部署锁后重新核对，不能沿用排队前的current、包或目标状态。
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
test "$(readlink "$base/current")" = "$baseline"
test -f "$archive"
test ! -L "$archive"
test "$(sha256sum "$archive" | cut -d' ' -f1)" = "$expected"
release=$base/releases/$release_id
test ! -e "$release"
test ! -L "$release"
stage_parent=$(mktemp -d "$base/releases/.workflow_stage_07.XXXXXXXX")
[[ $stage_parent == "$base/releases/.workflow_stage_07."* ]]
test "$(readlink -f "$stage_parent")" = "$stage_parent"
stage=$stage_parent/$release_id
mkdir -m 750 "$stage"
tar --no-same-owner --no-same-permissions -xzf "$archive" -C "$stage"
test "$(cat "$stage/release_id")" = "$release_id"
test "$(sha256sum "$stage/SHA256SUMS" | cut -d' ' -f1)" = "$release_id"
(cd "$stage" && sha256sum --quiet -c SHA256SUMS)
source "$stage/rk_board.sh"
verify_release "$stage"
dependencies=$stage_parent/runtime_dependencies.txt
test ! -e "$dependencies"
for object in "$stage/sn100_shell" "$stage/sn100_report_renderer" \
    "$stage/plugins/sqldrivers/libqsqlite.so" "$stage/plugins/platforms/libqoffscreen.so"; do
    test -f "$object"
    LD_LIBRARY_PATH="$stage/lib" ldd "$object" >>"$dependencies"
done
if grep -q 'not found' "$dependencies"; then
    cat "$dependencies"
    exit 1
fi
chown -R lckfb:lckfb "$stage"
test "$(readlink "$base/current")" = "$baseline"
test "$(sha256sum "$archive" | cut -d' ' -f1)" = "$expected"
test ! -e "$release"
test ! -L "$release"
mv --no-clobber -T -- "$stage" "$release"
test ! -e "$stage"
test "$(readlink "$base/current")" = "$baseline"
(cd "$release" && sha256sum --quiet -c SHA256SUMS)
printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\n'
date -Is
printf 'STAGED_RELEASE=%s\nCURRENT_RELEASE=%s\nACTIVATED=0\nPROGRAM_STARTED=0\n' "$release_id" "$baseline"
sha256sum "$release/sn100_shell" "$release/sn100_report_renderer"
printf 'PACKAGE_SHA256=%s\nDEPENDENCIES_BEGIN\n' "$expected"
cat "$dependencies"
printf 'DEPENDENCIES_END\n'
rm -f -- "$dependencies"
rmdir -- "$stage_parent"
rm -f -- "$archive"
printf 'STAGE_BATCH=integration_07\nSTAGE_SCRIPT_EXIT=0\n'
