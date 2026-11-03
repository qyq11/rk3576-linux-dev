#!/usr/bin/env bash
# 将板B日常资料切换为原Windows分类，保留原测试目录以便切回。
set -Eeuo pipefail
umask 077
test "$(tr -d '\000' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
test "$(id -u)" = 0
base=/home/rkshare/programs/sn100_shell
input=$base/verification/board_b_original_files_20261102
target=$base/runtime/data
previous=$base/runtime/previous_test_data_20261102
prepared=$base/runtime/original_data_prepared_20261102
test "$(readlink -f "$base")" = "$base"
test ! -L "$base/runtime" && test ! -L "$target"
test ! -e "$previous" && test ! -e "$prepared"
test ! -e "$base/original_project_root"
test -d "$target" && test "$(readlink -f "$target")" = "$target"
test "$(sha256sum "$input/board_b_original_files.tar.gz" | cut -d' ' -f1)" = 0be15da676790b1be74208fed38f37d646e8643d80afdd32286fd57301db3d30
exec 9>"$base/lifecycle.lock"
flock -n 9
! pgrep -x sn100_shell >/dev/null
! systemctl is-active --quiet sn100-shell.service
mkdir -m 700 "$prepared"
tar --no-same-owner --no-same-permissions -xzf "$input/board_b_original_files.tar.gz" -C "$prepared"
test "$(find "$prepared" -type f | wc -l)" = 141
test "$(find "$prepared" -type f -name '*.db' | wc -l)" = 29
test -z "$(find "$prepared" -type l -print -quit)"
(cd "$prepared" && sha256sum --quiet -c "$input/FILES.sha256")
printf 'sn100-original-work-v1\n' > "$prepared/.sn100-original-work-v1"
find "$prepared" -type d -exec chmod 700 {} +
find "$prepared" -type f -exec chmod 600 {} +
chown -R lckfb:lckfb "$prepared"
cp -p "$base/sn100.sh" "$input/sn100.sh.before"
sha256sum "$base/sn100.sh" > "$input/launcher_before.sha256"
find "$target" -type f -printf '%P\n' | sort > "$input/previous_test_file_list.txt"
readlink "$base/current" > "$input/current_release_before.txt"
mv "$target" "$previous"
mv "$prepared" "$target"
printf '%s\n' "$target" > "$base/original_project_root"
chmod 600 "$base/original_project_root"
chown lckfb:lckfb "$base/original_project_root"
install -m 700 -o lckfb -g lckfb "$input/rk_control.sh" "$base/sn100.sh"
bash -n "$base/sn100.sh"
(cd "$target" && sha256sum --quiet -c "$input/FILES.sha256")
sha256sum "$base/sn100.sh" > "$input/launcher_after.sha256"
printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\nORIGINAL_ROOT=%s\nPREVIOUS_TEST_ROOT=%s\nINSTALLED_ORIGINAL_FILES=141\nORIGINAL_DATABASES=29\nALL_FILE_HASHES_MATCH=1\nANONYMOUS_SESSION_DISABLED=1\nAPPLICATION_STARTED=0\nCONTROLLER_UNCHANGED=1\n' "$target" "$previous" | tee "$input/install_result.txt"
