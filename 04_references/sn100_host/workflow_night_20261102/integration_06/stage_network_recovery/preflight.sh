set -Eeuo pipefail
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
base=/home/rkshare/programs/sn100_shell
test "$(readlink "$base/current")" = 'releases/e31491a11e2e51764ab49c2c4435ce2f6f1f136223a76323d508ed613a758270'
test ! -e "$base/releases/14ce9714aaa0017c7ad78ff3f551d978672e5def79b96e817605295d7e59020c"
for item in "$base"/releases/.workflow_stage.*; do test ! -e "$item"; done
test "$(sha256sum '/home/rkshare/programs/sn100_shell/.workflow_upload_fe10b1c27c9f41bea1e265e3b9c5b8fa.tar.gz' | cut -d' ' -f1)" = 'd454ca929dcb77c1c5e41b18f84f29cb48cc52d66d3d2d01858f204478718b2d'
printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\nEXISTING_UPLOAD_HASH_OK=1\nNO_STAGE_OR_RELEASE=1\n'
date -Is
