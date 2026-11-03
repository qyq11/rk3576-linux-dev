set -Eeuo pipefail
test "$(id -u)" = 0
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
base=/home/rkshare/programs/sn100_shell
test "$(readlink -f "$base")" = "$base"
test "$(readlink "$base/current")" = 'releases/08bc43a73a9e8e4826a559f501829355357a46a999799e8b662ebd1feae1a0ef'
archive='/home/rkshare/programs/sn100_shell/.workflow_upload_09_adda59669a7a48669a6b92b52c21a4ac.tar.gz'
test ! -e "$archive"
test ! -L "$archive"
umask 077
set -o noclobber
cat >"$archive"
test "$(sha256sum "$archive" | cut -d' ' -f1)" = 'c3ecc9494a4de6cea81f5a9c91f3c6674ffa01a535a35f3ea0bea8944f70cbd0'
printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\nPACKAGE_SHA256=c3ecc9494a4de6cea81f5a9c91f3c6674ffa01a535a35f3ea0bea8944f70cbd0\nUPLOAD_SCRIPT_EXIT=0\n'
