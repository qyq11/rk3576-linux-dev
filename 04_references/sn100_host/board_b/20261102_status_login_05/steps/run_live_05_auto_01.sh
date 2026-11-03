#!/usr/bin/env bash
# 实际板B、原资料和原主控的状态专项，不发送开始/暂停/维护动作。
set -Eeuo pipefail
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
cd /home/rkshare/programs/sn100_shell/verification/status_login_05_20261102
bash run_case_on_board.sh auto \
 e31491a11e2e51764ab49c2c4435ce2f6f1f136223a76323d508ed613a758270 \
 609aa4b9a60109df4df241181e4622626ab9b77fba2b272ca0643a5bcf5506dc \
 8b48f0cf698cfaf8f1aeee40323e32ccc9201e2650072b217a25c34e1b1f1f05 \
 180 auto_01
