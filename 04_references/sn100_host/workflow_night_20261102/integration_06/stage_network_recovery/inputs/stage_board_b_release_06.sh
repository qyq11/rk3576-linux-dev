#!/usr/bin/env bash
# 只将已核指纹的新版本放到独立目录，不切换current或启动程序。
set -Eeuo pipefail
test "$(id -u)" = 0
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
test "$#" = 3
archive=$1
expected=$2
release_id=$3
base=/home/rkshare/programs/sn100_shell
[[ $archive == "$base/.workflow_upload_"*.tar.gz && $expected =~ ^[a-f0-9]{64}$ && $release_id =~ ^[a-f0-9]{64}$ ]]
test -f "$archive"
test ! -L "$archive"
test "$(sha256sum "$archive" | cut -d' ' -f1)" = "$expected"
test "$(readlink -f "$base")" = "$base"
test ! -L "$base/releases"
test -d "$base/releases"
exec 9>"$base/lifecycle.lock"
flock -n 9
previous=$(readlink "$base/current")
release=$base/releases/$release_id
test ! -e "$release"
stage_parent=$(mktemp -d "$base/releases/.workflow_stage.XXXXXXXX")
[[ $stage_parent == "$base/releases/.workflow_stage."* ]]
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
mv -- "$stage" "$release"
test "$(readlink "$base/current")" = "$previous"
printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\n'
date -Is
printf 'STAGED_RELEASE=%s\nCURRENT_RELEASE=%s\nACTIVATED=0\nPROGRAM_STARTED=0\n' "$release_id" "$previous"
sha256sum "$release/sn100_shell" "$release/sn100_report_renderer"
printf 'PACKAGE_SHA256=%s\nDEPENDENCIES_BEGIN\n' "$expected"
cat "$dependencies"
printf 'DEPENDENCIES_END\nSTAGE_BATCH=integration_06\n'
rm -f -- "$dependencies"
rmdir -- "$stage_parent"
rm -f -- "$archive"
