set -Eeuo pipefail
test "$(id -u)" = 0
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
base=/home/rkshare/programs/sn100_shell
test "$(readlink -f "$base")" = "$base"
test "$(readlink "$base/current")" = 'releases/14ce9714aaa0017c7ad78ff3f551d978672e5def79b96e817605295d7e59020c'
archive='/home/rkshare/programs/sn100_shell/.workflow_upload_07_76c48536118e49ab9b2b564c8e8a4740.tar.gz'
test ! -e "$archive"
test ! -L "$archive"
umask 077
set -o noclobber
cat >"$archive"
test "$(sha256sum "$archive" | cut -d' ' -f1)" = '0d387884c9fcba5012ddd897e571cfe579b8858561922fa2d980b23b04c885af'
printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\nPACKAGE_SHA256=0d387884c9fcba5012ddd897e571cfe579b8858561922fa2d980b23b04c885af\nUPLOAD_SCRIPT_EXIT=0\n'
