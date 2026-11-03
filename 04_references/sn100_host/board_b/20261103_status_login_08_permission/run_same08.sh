#!/usr/bin/env bash
set -Eeuo pipefail
cd /home/rkshare/programs/sn100_shell/verification/status_login_08_permission_20261103
[[ $(sha256sum review_inputs.sha256 | cut -d' ' -f1) == 700cd8eac45f621062cb11003565914158fe430e3beaf44a9076de2d7c81cb14 ]]
sha256sum --quiet -c review_inputs.sha256
bash run_case_on_board.sh auto 08bc43a73a9e8e4826a559f501829355357a46a999799e8b662ebd1feae1a0ef f6381c3cac13a3ca072bc7870bf5538b597bf87ec3d2b4bd40221e124e09ffdd 2968a11c54ec89a7c8a706fa4feb715fd48484d58ab41cc722b84d2ff74abb09 180 auto_permission_01
