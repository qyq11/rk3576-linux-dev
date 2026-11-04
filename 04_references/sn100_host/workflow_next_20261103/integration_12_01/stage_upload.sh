set -Eeuo pipefail
test "$(id -u)" = 0
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
base=/home/rkshare/programs/sn100_shell
test "$(readlink -f "$base")" = "$base"
test "$(readlink "$base/current")" = 'releases/c7cbf438a3a034b25ae22accfaf48bacf0717174ce85bef2203ce8db143cb06e'
store_lock=$base/immutable_store.lock
test ! -L "$store_lock"
if test -e "$store_lock"; then test -f "$store_lock"; fi
exec 9>>"$store_lock"
flock -n 9
test ! -L "$store_lock"
test "$store_lock" -ef "/proc/$$/fd/9"
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
test "$(readlink "$base/current")" = 'releases/c7cbf438a3a034b25ae22accfaf48bacf0717174ce85bef2203ce8db143cb06e'
archive='/home/rkshare/programs/sn100_shell/.workflow_upload_12_01_b90cc35137d04b818fea6eebb9df13ec.tar.gz'
test ! -e "$archive"
test ! -L "$archive"
umask 077
set -o noclobber
cat >"$archive"
test "$(sha256sum "$archive" | cut -d' ' -f1)" = '2834aaabfb492f0667e55c2f0a97a01be8fbbd3d100dd273ba46eb2f1ecdc687'
test "$(readlink "$base/current")" = 'releases/c7cbf438a3a034b25ae22accfaf48bacf0717174ce85bef2203ce8db143cb06e'
printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\nPACKAGE_SHA256=2834aaabfb492f0667e55c2f0a97a01be8fbbd3d100dd273ba46eb2f1ecdc687\nUPLOAD_SCRIPT_EXIT=0\n'
