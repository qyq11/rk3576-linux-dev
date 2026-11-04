"""生成总控一次停旧、唯一新启动及只读等待的脚本；本机不执行产品或联板。"""
from pathlib import Path
import json
import shlex

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
OUT = HERE / 'runtime_batch10_01'
CALLS = ROOT / 'temp/sn100_host/root_manager_20261103'
v = json.loads((OUT / 'root_prepare_values.json').read_text())
guard = dict(line.split('=', 1) for line in (ROOT / 'temp/sn100_host/workflow_next_20261103/integration_next_02/live_guard_before.txt').read_text().splitlines())
base = '/home/rkshare/programs/sn100_shell'
meta = base + '/verification/integration_next_02_runtime_01'
oldrun = base + '/runs/20261103_180023_24243'
text = (CALLS / 'prepare_flat_batch10_02.sh').read_text()
functions = text[text.index('checked_record()'):text.index('checked_record "$root_transition_approval"')]
shell_values = dict(v)
shell_values.update(base=base, meta=meta, flat=base + '/releases/' + v['release_id'],
                    oldrun=oldrun, old_pid=guard['MAIN_PID'], old_ticks=guard['MAIN_START_TICKS'],
                    old_exe=guard['MAIN_EXE'], old_exe_sha=guard['MAIN_EXE_SHA256'],
                    expected_boot=guard['BOOT_ID'],
                    controller_expected_identity=':'.join(guard[key] for key in (
                        'BOOT_ID', 'CONTROLLER_PID', 'CONTROLLER_START_TICKS', 'CONTROLLER_UID',
                        'CONTROLLER_EXE', 'CONTROLLER_EXE_SHA256', 'CONTROLLER_LISTEN_ENDPOINTS')))
prefix = 'set -Eeuo pipefail\nset -o noclobber\numask 077\n'
prefix += ''.join(key + '=' + shlex.quote(value) + '\n' for key, value in shell_values.items())
prefix += functions
prefix += '''
root_assert() {
    [[ $(tr -d '\\0\\n' </proc/device-tree/serial-number) == 8314b1e8ffd0321f ]]
    [[ $(cat /proc/sys/kernel/random/boot_id) == "$expected_boot" ]]
    [[ $(readlink "$base/current") == "$1" ]]
    checked_record "$root_transition_approval" "$root_transition_approval_sha256"
    checked_record "$root_launch_intent" "$root_launch_intent_sha256"
    checked_record "$configuration_manifest" "$configuration_manifest_sha256"
    sha256sum --quiet -c "$configuration_manifest"
'''
for stem in ('ORIGINAL_PROJECT_ROOT', 'INSTRUMENT_CONFIGURATION_FILE', 'THEME_CONFIGURATION_FILE'):
    prefix += "    printf '%s  %s\\n' " + shlex.quote(guard['POINTER_' + stem + '_SHA256']) + ' ' + shlex.quote(guard['POINTER_' + stem + '_PATH']) + ' | sha256sum --quiet -c -\n'
prefix += '''    [[ $(sha256sum "$flat/rk_board.sh" | cut -d' ' -f1) == "$expected_board_script_sha256" ]]
    [[ $(sha256sum "$flat/SHA256SUMS" | cut -d' ' -f1) == "$release_id" ]]
    (cd "$flat" && sha256sum --quiet -c SHA256SUMS)
    check_controller
    [[ $controller_identity == "$controller_expected_identity" ]]
}
'''

preflight = prefix + '''
root_assert "$expected_current"
/usr/sbin/runuser -u lckfb -- /bin/bash -c 'source "$1"; preflight "$2" "$3"' preflight "$flat/rk_board.sh" "$base" "$flat"
[[ $(readlink /proc/$old_pid/exe) == "$old_exe" && $(transition_start_ticks "$old_pid") == "$old_ticks" ]]
printf 'NEW_RELEASE_PREFLIGHT_PASSED=1\\nOLD_APPLICATION_STILL_RUNNING=1\\n'
'''

stop = prefix + '''
[[ ! -L $base/immutable_store.lock ]]
exec 8>>"$base/immutable_store.lock"
flock -n 8
root_assert "$expected_current"
[[ $(cat "$base/latest_run") == "${oldrun##*/}" && $(readlink -f "$oldrun") == "$oldrun" ]]
[[ $(cat "$oldrun/app.pid") == "$old_pid" && $(cat "$oldrun/app.start_ticks") == "$old_ticks" && ! -e $oldrun/exit_code ]]
[[ $(readlink /proc/$old_pid/exe) == "$old_exe" && $(transition_start_ticks "$old_pid") == "$old_ticks" ]]
[[ $(sha256sum /proc/$old_pid/exe | cut -d' ' -f1) == "$old_exe_sha" && $(transition_uid "$old_pid") == "$(id -u lckfb)" ]]
[[ ! -e $meta/old_stop.stdout.log && ! -e $meta/old_stop.stderr.log && ! -e $old_stop_receipt ]]
stop_code=0
# 只此一次正常关闭旧版；失败不强杀、不再发信号。
/usr/sbin/runuser -u lckfb -- /bin/bash "$base/$expected_current/rk_board.sh" stop "$base" >"$meta/old_stop.stdout.log" 2>"$meta/old_stop.stderr.log" || stop_code=$?
printf '%s\\n' "$stop_code" > "$meta/old_stop.actual_exit"
printf 'OLD_STOP_ACTUAL_EXIT=%s\\n' "$stop_code"
[[ $stop_code == 0 ]]
[[ -f $oldrun/exit_code && $(cat "$oldrun/exit_code") == 0 ]]
grep -qx 'APP_EXIT=0' "$oldrun/result.txt"
grep -qx 'CLEAN_SHUTDOWN=1' "$oldrun/result.txt"
# 只等旧服务自然退出及显示释放，不调用systemctl stop或额外信号。
quiet=0
for ((i=0;i<4;i++)); do
    if transition_assert_quiet; then quiet=1; break; fi
    sleep 0.1
done
[[ $quiet == 1 ]]
[[ -f $base/lifecycle.lock && ! -L $base/lifecycle.lock ]]
exec 9<>"$base/lifecycle.lock"
flock -n 9
root_assert "$expected_current"
transition_assert_quiet
{
    printf 'FORMAT=sn100-root-old-stop-receipt-v1\\nBOARD=board_b\\nSERIAL=8314b1e8ffd0321f\\nUTC='; date -u +%Y-%m-%dT%H:%M:%SZ
    printf 'BOOT_ID=%s\\nOLD_RUN=%s\\nOLD_PID=%s\\nOLD_START_TICKS=%s\\n' "$expected_boot" "$oldrun" "$old_pid" "$old_ticks"
    printf 'OLD_EXE=%s\\nOLD_EXE_SHA256=%s\\n' "$old_exe" "$old_exe_sha"
    cat "$oldrun/result.txt"
    printf 'STOP_TOOL_ACTUAL_EXIT=%s\\nDISPLAY_RELEASED=1\\nCURRENT_UNCHANGED=%s\\nCONTROLLER_IDENTITY=%s\\n' "$stop_code" "$expected_current" "$controller_identity"
    printf 'ROOT_LAUNCH_INTENT_SHA256=%s\\nCONFIGURATION_MANIFEST_SHA256=%s\\n' "$root_launch_intent_sha256" "$configuration_manifest_sha256"
} > "$old_stop_receipt"
printf 'OLD_STOP_RECEIPT_SHA256='; sha256sum "$old_stop_receipt" | cut -d' ' -f1
cat "$old_stop_receipt"
'''

launch = prefix + '''
exec 8>>"$base/immutable_store.lock"
flock -n 8
[[ -f $base/lifecycle.lock && ! -L $base/lifecycle.lock ]]
exec 9<>"$base/lifecycle.lock"
flock -n 9
root_assert "releases/$release_id"
transition_assert_quiet
audit=$base/verification/transition_next_02_$release_id
[[ -d $audit && ! -L $audit && $(readlink -f "$audit") == "$audit" ]]
[[ $(cat "$audit/root_launch_intent.sha256") == "$root_launch_intent_sha256" ]]
[[ $(sha256sum "$old_stop_receipt" | cut -d' ' -f1) == "$(cat "$audit/old_stop_receipt.sha256")" ]]
[[ -d $meta/observation && ! -L $meta/observation && $(readlink -f "$meta/observation") == "$meta/observation" ]]
[[ $(stat -c %u "$meta/observation") == "$(id -u lckfb)" && $(stat -c %a "$meta/observation") == 700 ]]
[[ -z $(find "$meta/observation" -mindepth 1 -maxdepth 1 -print -quit) ]]
[[ $(stat -c %u "$base") == "$(id -u lckfb)" && ! -L $base/runs && $(readlink -f "$base/runs") == "$base/runs" ]]
[[ ! -e $meta/captures && ! -L $meta/captures ]]
mkdir -m 700 "$meta/captures"
chown lckfb:lckfb "$meta/captures"
run="$base/runs/$(date +%Y%m%d_%H%M%S)_$$"
[[ ! -e $run && ! -L $run ]]
mkdir -m 700 "$run"
chown lckfb:lckfb "$run"
[[ $(cat "$base/latest_run") == "${oldrun##*/}" ]]
cp -- "$base/latest_run" "$meta/latest_run_before_start.txt"
latest_new="$base/.latest_run_batch10_$$"
[[ ! -e $latest_new && ! -L $latest_new ]]
printf '%s\\n' "${run##*/}" > "$latest_new"
chown lckfb:lckfb "$latest_new"
chmod 600 "$latest_new"
mv -T -- "$latest_new" "$base/latest_run"
printf '%s\\n' "$run" > "$meta/launched_run.txt"
# 仅分配一个系统服务。监督器取得同一把锁后启动；没有start外层30秒关闭分支。
systemd-run --quiet --collect --unit=sn100-shell --property=Type=exec \
    /usr/bin/openvt -c 7 -s -w -- /usr/sbin/runuser -u lckfb -- \
    /usr/bin/env SN100_LIS_OBSERVATION_DIR="$meta/observation" \
    SN100_LIS_OBSERVATION_ID=integration_next_02_lis_01 SN100_LIS_OBSERVATION_RELEASE="$release_id" \
    /bin/bash "$flat/rk_board.sh" supervise "$base" "$flat" "$run" 0 ''
exec 9>&-
exec 8>&-
# 这里只等待身份文件，不要求READY；超时仅返回本工具失败，绝不关闭程序。
identified=0
for ((i=0;i<60;i++)); do
    if [[ -s $run/app.pid && -s $run/app.start_ticks ]]; then identified=1; break; fi
    sleep 0.25
done
[[ $identified == 1 && ! -e $run/exit_code ]]
pid=$(cat "$run/app.pid")
[[ $pid =~ ^[1-9][0-9]*$ && $(transition_start_ticks "$pid") == "$(cat "$run/app.start_ticks")" ]]
[[ $(readlink /proc/$pid/exe) == "$flat/sn100_shell" && $(transition_uid "$pid") == "$(id -u lckfb)" ]]
[[ $(sha256sum /proc/$pid/exe | cut -d' ' -f1) == "$expected_app_sha256" ]]
root_assert "releases/$release_id"
printf 'UNIQUE_NEW_APPLICATION_STARTED=1\\nRUN_DIR=%s\\nAPP_PID=%s\\nSTART_TICKS=%s\\nBOOT_ID=%s\\nSOFT_DEADLINE_SECONDS=0\\n' "$run" "$pid" "$(cat "$run/app.start_ticks")" "$expected_boot"
printf 'APPLICATION_EXIT=not_evaluated\\nPROGRAM_LEFT_RUNNING=1\\n'
'''

wait = prefix + '''
root_assert "releases/$release_id"
run=$(cat "$meta/launched_run.txt")
[[ $run == "$base/runs/"* && ${run#"$base/runs/"} =~ ^[0-9_]+$ && $(readlink -f "$run") == "$run" ]]
[[ ! -e $run/exit_code ]]
pid=$(cat "$run/app.pid")
ticks=$(cat "$run/app.start_ticks")
[[ $(readlink /proc/$pid/exe) == "$flat/sn100_shell" && $(transition_start_ticks "$pid") == "$ticks" ]]
code=0
/usr/sbin/runuser -u lckfb -- /bin/bash "$meta/wait_lis_observation.sh" \
    --result-directory "$meta/observation" --capture-directory "$meta/captures/capture_01" \
    --pid "$pid" --start-ticks "$ticks" --release-id "$release_id" --executable "$flat/sn100_shell" \
    --executable-sha256 "$expected_app_sha256" --observation-id integration_next_02_lis_01 \
    --timeout-seconds 120 --boot-id "$expected_boot" --board-serial 8314b1e8ffd0321f \
    --current-link "$base/current" --expected-current-target "$flat" --release-id-file "$flat/release_id" || code=$?
printf '%s\\n' "$code" > "$meta/waiter.actual_exit"
printf 'WAITER_ACTUAL_EXIT=%s\\n' "$code"
root_assert "releases/$release_id"
[[ ! -e $run/exit_code && $(readlink /proc/$pid/exe) == "$flat/sn100_shell" && $(transition_start_ticks "$pid") == "$ticks" ]]
printf 'POST_WAITER_SAME_APPLICATION_RUNNING=1\\n'
exit "$code"
'''

for name, body in (('preflight_new_batch10_02', preflight), ('stop_old_batch10_02', stop),
                   ('launch_new_batch10_02', launch), ('wait_lis_batch10_02', wait)):
    with (CALLS / (name + '.sh')).open('x', encoding='utf-8', newline='\n') as stream:
        stream.write(body)
print('RUNTIME_CONTROL_SCRIPTS_PREPARED=1; BOARD_EXECUTED=0')
