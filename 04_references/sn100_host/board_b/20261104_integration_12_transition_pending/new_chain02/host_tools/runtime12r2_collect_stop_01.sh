set -Eeuo pipefail
[[ $(cat /proc/sys/kernel/random/boot_id) == 0eb94389-f120-42ad-9042-9c21d96a1481 ]]
[[ $(readlink /home/rkshare/programs/sn100_shell/current) == releases/c7cbf438a3a034b25ae22accfaf48bacf0717174ce85bef2203ce8db143cb06e ]]
[[ -f /home/rkshare/programs/sn100_shell/runs/20261103_221819_662833/result.txt && ! -L /home/rkshare/programs/sn100_shell/runs/20261103_221819_662833/result.txt ]]
[[ $(stat -c %s /home/rkshare/programs/sn100_shell/runs/20261103_221819_662833/result.txt) -le 16777216 ]]
printf 'BEGIN_STOP_FILE=result\n'
sha256sum -- /home/rkshare/programs/sn100_shell/runs/20261103_221819_662833/result.txt
base64 -w0 -- /home/rkshare/programs/sn100_shell/runs/20261103_221819_662833/result.txt
printf '\nEND_STOP_FILE\n'
[[ -f /home/rkshare/programs/sn100_shell/runs/20261103_221819_662833/exit_code && ! -L /home/rkshare/programs/sn100_shell/runs/20261103_221819_662833/exit_code ]]
[[ $(stat -c %s /home/rkshare/programs/sn100_shell/runs/20261103_221819_662833/exit_code) -le 16777216 ]]
printf 'BEGIN_STOP_FILE=exit_code\n'
sha256sum -- /home/rkshare/programs/sn100_shell/runs/20261103_221819_662833/exit_code
base64 -w0 -- /home/rkshare/programs/sn100_shell/runs/20261103_221819_662833/exit_code
printf '\nEND_STOP_FILE\n'
[[ -f /home/rkshare/programs/sn100_shell/verification/integration_12_01_runtime_02/phase_logs/stop/stdout.log && ! -L /home/rkshare/programs/sn100_shell/verification/integration_12_01_runtime_02/phase_logs/stop/stdout.log ]]
[[ $(stat -c %s /home/rkshare/programs/sn100_shell/verification/integration_12_01_runtime_02/phase_logs/stop/stdout.log) -le 16777216 ]]
printf 'BEGIN_STOP_FILE=stdout\n'
sha256sum -- /home/rkshare/programs/sn100_shell/verification/integration_12_01_runtime_02/phase_logs/stop/stdout.log
base64 -w0 -- /home/rkshare/programs/sn100_shell/verification/integration_12_01_runtime_02/phase_logs/stop/stdout.log
printf '\nEND_STOP_FILE\n'
[[ -f /home/rkshare/programs/sn100_shell/verification/integration_12_01_runtime_02/phase_logs/stop/stderr.log && ! -L /home/rkshare/programs/sn100_shell/verification/integration_12_01_runtime_02/phase_logs/stop/stderr.log ]]
[[ $(stat -c %s /home/rkshare/programs/sn100_shell/verification/integration_12_01_runtime_02/phase_logs/stop/stderr.log) -le 16777216 ]]
printf 'BEGIN_STOP_FILE=stderr\n'
sha256sum -- /home/rkshare/programs/sn100_shell/verification/integration_12_01_runtime_02/phase_logs/stop/stderr.log
base64 -w0 -- /home/rkshare/programs/sn100_shell/verification/integration_12_01_runtime_02/phase_logs/stop/stderr.log
printf '\nEND_STOP_FILE\n'
[[ -f /home/rkshare/programs/sn100_shell/verification/integration_12_01_runtime_02/phase_logs/stop/actual_exit && ! -L /home/rkshare/programs/sn100_shell/verification/integration_12_01_runtime_02/phase_logs/stop/actual_exit ]]
[[ $(stat -c %s /home/rkshare/programs/sn100_shell/verification/integration_12_01_runtime_02/phase_logs/stop/actual_exit) -le 16777216 ]]
printf 'BEGIN_STOP_FILE=actual_exit\n'
sha256sum -- /home/rkshare/programs/sn100_shell/verification/integration_12_01_runtime_02/phase_logs/stop/actual_exit
base64 -w0 -- /home/rkshare/programs/sn100_shell/verification/integration_12_01_runtime_02/phase_logs/stop/actual_exit
printf '\nEND_STOP_FILE\n'
[[ -f /home/rkshare/programs/sn100_shell/runs/20261103_221819_662833/app.pid && ! -L /home/rkshare/programs/sn100_shell/runs/20261103_221819_662833/app.pid ]]
[[ $(stat -c %s /home/rkshare/programs/sn100_shell/runs/20261103_221819_662833/app.pid) -le 16777216 ]]
printf 'BEGIN_STOP_FILE=app_pid\n'
sha256sum -- /home/rkshare/programs/sn100_shell/runs/20261103_221819_662833/app.pid
base64 -w0 -- /home/rkshare/programs/sn100_shell/runs/20261103_221819_662833/app.pid
printf '\nEND_STOP_FILE\n'
[[ -f /home/rkshare/programs/sn100_shell/runs/20261103_221819_662833/app.start_ticks && ! -L /home/rkshare/programs/sn100_shell/runs/20261103_221819_662833/app.start_ticks ]]
[[ $(stat -c %s /home/rkshare/programs/sn100_shell/runs/20261103_221819_662833/app.start_ticks) -le 16777216 ]]
printf 'BEGIN_STOP_FILE=app_start_ticks\n'
sha256sum -- /home/rkshare/programs/sn100_shell/runs/20261103_221819_662833/app.start_ticks
base64 -w0 -- /home/rkshare/programs/sn100_shell/runs/20261103_221819_662833/app.start_ticks
printf '\nEND_STOP_FILE\n'
[[ -f /home/rkshare/programs/sn100_shell/runs/20261103_221819_662833/app.exe && ! -L /home/rkshare/programs/sn100_shell/runs/20261103_221819_662833/app.exe ]]
[[ $(stat -c %s /home/rkshare/programs/sn100_shell/runs/20261103_221819_662833/app.exe) -le 16777216 ]]
printf 'BEGIN_STOP_FILE=app_exe\n'
sha256sum -- /home/rkshare/programs/sn100_shell/runs/20261103_221819_662833/app.exe
base64 -w0 -- /home/rkshare/programs/sn100_shell/runs/20261103_221819_662833/app.exe
printf '\nEND_STOP_FILE\n'
[[ -f /home/rkshare/programs/sn100_shell/runs/20261103_221819_662833/boot_id && ! -L /home/rkshare/programs/sn100_shell/runs/20261103_221819_662833/boot_id ]]
[[ $(stat -c %s /home/rkshare/programs/sn100_shell/runs/20261103_221819_662833/boot_id) -le 16777216 ]]
printf 'BEGIN_STOP_FILE=boot_id\n'
sha256sum -- /home/rkshare/programs/sn100_shell/runs/20261103_221819_662833/boot_id
base64 -w0 -- /home/rkshare/programs/sn100_shell/runs/20261103_221819_662833/boot_id
printf '\nEND_STOP_FILE\n'
