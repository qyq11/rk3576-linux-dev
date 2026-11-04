set -Eeuo pipefail
base=/home/rkshare/programs/sn100_shell
[[ $(cat /proc/sys/kernel/random/boot_id) == 0eb94389-f120-42ad-9042-9c21d96a1481 ]]
sha256sum --quiet -c "$base/verification/integration_12_01_runtime_02/configuration.sha256"
printf 'CONFIGURATION_UNCHANGED=1\nUTC='; date -u +%Y-%m-%dT%H:%M:%SZ
old=$base/runs/20261103_221819_662833
stat -c 'APP_LOG_BYTES=%s' "$old/app.log"
printf 'LIFECYCLE_LOG_LINES_BEGIN\n'
grep -aE 'SHUTDOWN_REQUEST|SHUTDOWN_ACCEPTED|SHUTDOWN_REJECTED|EXIT_BUTTON_|SHELL_READY|HOSPITAL_STORE_CLOSED|ACCOUNT_STORE_CLOSED|HISTORY_STORE_CLOSED|THREADS_STOPPED|LOGIN_(REQUEST|PENDING|FINISHED)' "$old/app.log" | tail -n 60 || true
printf 'LIFECYCLE_LOG_LINES_END\n'
printf 'SIGNAL_STATUS_BEGIN\n'
awk '/^State:|^SigPnd:|^ShdPnd:|^SigBlk:|^SigIgn:|^SigCgt:/{print}' /proc/669382/status
printf 'SIGNAL_STATUS_END\nREAD_ONLY=1\nNO_ADDITIONAL_CLOSE_REQUEST=1\n'
