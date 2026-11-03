#!/usr/bin/env bash
# 修复原试剂历史数值的读取；沿用已经核对的原资料、连接指针和启动器。
set -Eeuo pipefail
test "$(id -u)" = 0
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
test "$#" = 3
archive=$1; expected=$2; release_id=$3
base=/home/rkshare/programs/sn100_shell
check=$base/verification/sn500_connection_20261102/upgrade_reagent_fix
[[ $archive == "$base/"*.tar.gz && $expected =~ ^[a-f0-9]{64}$ && $release_id =~ ^[a-f0-9]{64}$ ]]
test "$(sha256sum "$archive" | cut -d' ' -f1)" = "$expected"
test "$(readlink "$base/current")" = releases/120579b7a3b7224f37e2b6451c1080b565bb494edadfc34949efea9f24c9df46
test "$(cat "$base/original_project_root")" = "$base/runtime/data"
test "$(cat "$base/instrument_configuration_file")" = "$base/runtime/data/config.ini"
test "$(cat "$base/theme_configuration_file")" = "$base/runtime/data/theme_config.ini"
test "$(sha256sum "$base/runtime/data/theme_config.ini" | cut -d' ' -f1)" = 029605f35924de646b5402f1e377c93cb26e01952e0daa5fda300fe47f7feed7
test ! -e "$check"
exec 9>"$base/lifecycle.lock"
flock -n 9
for exe in /proc/[0-9]*/exe; do
    program=$(readlink "$exe" || true)
    case "$program" in "$base"/releases/*/sn100_shell) printf '上位机仍在运行，未安装。\n'; exit 1 ;; esac
done
install -d -m 750 -o lckfb -g lckfb "$check"
readlink "$base/current" >"$check/previous_release.txt"
sha256sum "$base/sn100.sh" "$base/original_project_root" "$base/instrument_configuration_file" "$base/theme_configuration_file" >"$check/retained_files.sha256"
stage_parent=$base/releases/.sn500_fix_stage_20261102
test ! -e "$stage_parent"
mkdir -m 750 "$stage_parent"
stage=$stage_parent/$release_id
mkdir -m 750 "$stage"
tar --no-same-owner --no-same-permissions -xzf "$archive" -C "$stage"
chown -R lckfb:lckfb "$stage_parent"
source "$stage/rk_board.sh"
verify_release "$stage"
{
    for object in "$stage/sn100_shell" "$stage/sn100_report_renderer" "$stage/plugins/sqldrivers/libqsqlite.so"; do
        LD_LIBRARY_PATH="$stage/lib" ldd "$object"
    done
} >"$check/dependencies.log"
! grep -q 'not found' "$check/dependencies.log"
test ! -e "$base/releases/$release_id"
mv -- "$stage" "$base/releases/$release_id"
rmdir -- "$stage_parent"
ln -s "releases/$release_id" "$base/.current_sn500_fix_20261102"
mv -Tf -- "$base/.current_sn500_fix_20261102" "$base/current"
verify_release "$base/releases/$release_id"
sha256sum -c "$check/retained_files.sha256"
{
    date -Is
    printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\nRELEASE_ID=%s\n' "$release_id"
    printf 'APP_SHA256='; sha256sum "$base/current/sn100_shell"
    printf 'THEME_SHA256='; sha256sum "$base/runtime/data/theme_config.ini"
    printf 'ORIGINAL_DATA_ROOT=%s\nRETAINED_FILES_UNCHANGED=1\n' "$(cat "$base/original_project_root")"
} >"$check/deployment_result.txt"
cat "$check/deployment_result.txt"
