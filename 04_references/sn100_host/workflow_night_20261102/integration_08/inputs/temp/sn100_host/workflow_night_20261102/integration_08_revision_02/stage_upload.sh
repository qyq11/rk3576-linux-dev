set -Eeuo pipefail
test "$(id -u)" = 0
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
base=/home/rkshare/programs/sn100_shell
test "$(readlink -f "$base")" = "$base"
test "$(readlink "$base/current")" = 'releases/94d1b9794f2d8f1ff776b498e62d29ba2ebd82fef807d7cda70b05837c3eccb1'
archive='/home/rkshare/programs/sn100_shell/.workflow_upload_08_33d9f214defb4214a641faccfcf235a5.tar.gz'
test ! -e "$archive"
test ! -L "$archive"
umask 077
set -o noclobber
cat >"$archive"
test "$(sha256sum "$archive" | cut -d' ' -f1)" = 'f7296737749a238c0360311617f78d7cf3d85443081efe4eb240f34a7054fcbf'
printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\nPACKAGE_SHA256=f7296737749a238c0360311617f78d7cf3d85443081efe4eb240f34a7054fcbf\nUPLOAD_SCRIPT_EXIT=0\n'
