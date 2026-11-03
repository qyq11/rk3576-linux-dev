set -Eeuo pipefail
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
test ! -e '/home/rkshare/programs/sn100_shell/verification/status_login_05_20261102'
install -d -m 750 -o lckfb -g lckfb '/home/rkshare/programs/sn100_shell/verification/status_login_05_20261102'
