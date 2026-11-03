#!/usr/bin/env bash
# 仅暂存第09批；任何失败都保留现场，不切current，不启停程序。
set -Eeuo pipefail
test "$(id -u)" = 0
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
test "$#" = 4
archive=$1
expected=$2
release_id=$3
base=/home/rkshare/programs/sn100_shell
baseline=$4
[[ $baseline =~ ^releases/[a-f0-9]{64}$ ]]
[[ $archive =~ ^/home/rkshare/programs/sn100_shell/\.workflow_upload_09_[a-f0-9]{32}\.tar\.gz$ ]]
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
namespace=$base/releases/integration_09
# 只有实际执行且已取得部署锁，才创建本批独立父目录；旧批次目录只读不改。
test "$(readlink -f "$base/releases")" = "$base/releases"
test ! -L "$namespace"
if test -e "$namespace"; then
    test -d "$namespace"
else
    mkdir -m 750 -- "$namespace"
    chown lckfb:lckfb "$namespace"
fi
test "$(readlink -f "$namespace")" = "$namespace"
release=$namespace/$release_id
test ! -e "$release"
test ! -L "$release"
for previous_stage in "$namespace"/.workflow_stage_09.*; do
    if test -e "$previous_stage" || test -L "$previous_stage"; then
        printf '第09批已有未结清暂存现场，未覆盖或重跑：%s\n' "$previous_stage" >&2
        exit 96
    fi
done
stage_parent=$(mktemp -d "$namespace/.workflow_stage_09.XXXXXXXX")
[[ $stage_parent == "$namespace/.workflow_stage_09."* ]]
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
test ! -L "$base/releases"
test "$(readlink -f "$base/releases")" = "$base/releases"
test ! -L "$namespace"
test "$(readlink -f "$namespace")" = "$namespace"
test ! -L "$stage_parent"
test "$(readlink -f "$stage_parent")" = "$stage_parent"
test ! -e "$release"
test ! -L "$release"
mv --no-clobber -T -- "$stage" "$release"
test ! -e "$stage"
test "$(readlink "$base/current")" = "$baseline"
test ! -L "$release"
test "$(readlink -f "$release")" = "$release"
(cd "$release" && sha256sum --quiet -c SHA256SUMS)
printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\n'
date -Is
printf 'STAGED_RELEASE=%s\nCURRENT_RELEASE=%s\nREMOTE_RELEASE=%s\nACTIVATED=0\nPROGRAM_STARTED=0\n' "$release_id" "$baseline" "$release"
sha256sum "$release/sn100_shell" "$release/sn100_report_renderer"
printf 'PACKAGE_SHA256=%s\nDEPENDENCIES_BEGIN\n' "$expected"
cat "$dependencies"
printf 'DEPENDENCIES_END\n'
rm -f -- "$dependencies"
rmdir -- "$stage_parent"
rm -f -- "$archive"
printf 'STAGE_BATCH=integration_09\nSTAGE_SCRIPT_EXIT=0\n'
