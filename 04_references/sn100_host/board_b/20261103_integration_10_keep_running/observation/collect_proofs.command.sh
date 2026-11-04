set -Eeuo pipefail
[[ $(tr -d '\0\n' </proc/device-tree/serial-number) == 8314b1e8ffd0321f ]]
[[ $(cat /proc/sys/kernel/random/boot_id) == 0eb94389-f120-42ad-9042-9c21d96a1481 ]]
tar -czf - -C /home/rkshare/programs/sn100_shell/verification/integration_next_02_runtime_01 -- configuration.sha256 latest_run_before_start.txt launched_run.txt old_stop.actual_exit old_stop_receipt.txt resources_snapshot_01.tsv root_approval.md runtime_launch_intent.json waiter.actual_exit
