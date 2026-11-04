set -Eeuo pipefail
test "$(id -u)" = 0
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
base=/home/rkshare/programs/sn100_shell
test "$(readlink -f "$base")" = "$base"
test "$(readlink "$base/current")" = 'releases/b31f581fd240e09ee4c0a4a4eb6c4cbdd674fa9cf8897fcd170e9d0ca058d993'
store_lock=$base/immutable_store.lock
test ! -L "$store_lock"
if test -e "$store_lock"; then test -f "$store_lock"; fi
exec 9>>"$store_lock"
flock -n 9
test ! -L "$store_lock"
test "$store_lock" -ef "/proc/$$/fd/9"
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
test "$(readlink "$base/current")" = 'releases/b31f581fd240e09ee4c0a4a4eb6c4cbdd674fa9cf8897fcd170e9d0ca058d993'
archive='/home/rkshare/programs/sn100_shell/.workflow_upload_next_478b1cc677f944c7b722f3a59d280324.tar.gz'
test ! -e "$archive"
test ! -L "$archive"
umask 077
set -o noclobber
cat >"$archive"
test "$(sha256sum "$archive" | cut -d' ' -f1)" = '0cfa398b008df64ebc92aae17e9837633b675adccee73c65083db08b255187ee'
test "$(readlink "$base/current")" = 'releases/b31f581fd240e09ee4c0a4a4eb6c4cbdd674fa9cf8897fcd170e9d0ca058d993'
printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\nPACKAGE_SHA256=0cfa398b008df64ebc92aae17e9837633b675adccee73c65083db08b255187ee\nUPLOAD_SCRIPT_EXIT=0\n'
