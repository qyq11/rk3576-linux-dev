set -Eeuo pipefail
[[ $(tr -d '\0\n' </proc/device-tree/serial-number) == 8314b1e8ffd0321f ]]
[[ $(cat /proc/sys/kernel/random/boot_id) == 0eb94389-f120-42ad-9042-9c21d96a1481 ]]
test ! -L /home/rkshare/programs/sn100_shell/verification/integration_next_02_runtime_01/captures/capture_01
tar -czf - -C /home/rkshare/programs/sn100_shell/verification/integration_next_02_runtime_01/captures/capture_01 -- capture_identity_after.json capture_identity_before.json capture_manifest.sha256 capture_status.json lis_observation.json lis_observation.ready
