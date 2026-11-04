set -Eeuo pipefail
base=/home/rkshare/programs/sn100_shell
[[ $(cat /proc/sys/kernel/random/boot_id) == 0eb94389-f120-42ad-9042-9c21d96a1481 ]]
sha256sum --quiet -c "$base/verification/integration_12_01_runtime_01/configuration.sha256"
printf 'CONFIGURATION_UNCHANGED=1\nUTC='; date -u +%Y-%m-%dT%H:%M:%SZ
systemctl show opto-camera.service -p Id -p ActiveState -p SubState -p MainPID -p NRestarts -p Restart -p ExecMainStartTimestamp -p ExecMainStartTimestampMonotonic -p InvocationID -p Result -p ExecMainCode -p ExecMainStatus -p FragmentPath
pid=$(systemctl show opto-camera.service -p MainPID --value)
[[ $pid =~ ^[1-9][0-9]*$ ]]
printf 'ACTUAL_CAMERA_PID=%s\n' "$pid"
cat "/proc/$pid/stat"
awk '/^Uid:/{print}' "/proc/$pid/status"
readlink "/proc/$pid/exe"
sha256sum "/proc/$pid/exe"
journalctl -u opto-camera.service --since '2026-11-04 09:35:00' -n 90 --no-pager -o short-iso
printf 'READ_ONLY=1\nNO_CAMERA_START_STOP=1\nNO_PROGRAM_SIGNAL=1\n'
