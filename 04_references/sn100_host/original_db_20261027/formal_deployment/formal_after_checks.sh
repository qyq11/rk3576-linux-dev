#!/usr/bin/env bash
set -Eeuo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$here/../../../.." && pwd)
module=$root/06_programs/sn100_host/shell
work=$here/release_work_02
while ! grep -qx 'BOTH_BUTTON_GUI_TESTS_PASSED=1' "$here/gui_remaining.log"; do sleep 2; done
test "$(cat "$here/page_10/exit_code")" = 0
test "$(cat "$here/page_11/exit_code")" = 0
printf 'FORMAL_STEP=deploy\n'
bash "$here/deploy_prepared_02.sh" > "$here/formal_deploy.log" 2>&1
printf 'FORMAL_STEP=install_original_files\n'
bash "$root/06_programs/sn100_host/original_files/install.sh" > "$here/formal_original_install.log" 2>&1
SN100_RK_WORK_DIR=$work bash "$module/rk.sh" check > "$here/formal_preflight.log" 2>&1
printf 'FORMAL_STEP=first_start\n'
SN100_RK_WORK_DIR=$work bash "$module/rk.sh" start 40 offlineAdmin > "$here/formal_first_start.log" 2>&1
completed=0
for ((i=0;i<90;++i)); do
    if ssh -o BatchMode=yes -o ConnectTimeout=6 lckfb@192.168.100.123 bash -s <<'REMOTE'
set -Eeuo pipefail
base=/home/rkshare/programs/sn100_shell
run=$base/runs/$(cat "$base/latest_run")
test -f "$run/exit_code"
test "$(cat "$run/exit_code")" = 0
grep -qx SHELL_READY "$run/app.log"
grep -qx 'CLEAN_SHUTDOWN=1' "$run/result.txt"
REMOTE
    then completed=1; break; fi
    sleep 1
done
[[ $completed == 1 ]] || { echo 'Formal program did not confirm clean shutdown.' >&2; exit 1; }
SN100_RK_WORK_DIR=$work bash "$module/rk.sh" status > "$here/formal_first_status.log" 2>&1
SN100_RK_WORK_DIR=$work bash "$module/rk.sh" collect > "$here/formal_first_collect.log" 2>&1
ssh -o BatchMode=yes -o ConnectTimeout=6 lckfb@192.168.100.123 bash -s > "$here/formal_data_preservation.log" <<'REMOTE'
set -Eeuo pipefail
base=/home/rkshare/programs/sn100_shell
staging=/home/rkshare/programs/sn100_original/20261027_01
cd "$base/runtime/data"
sha256sum --quiet -c "$staging/original.SHA256SUMS"
printf 'ORIGINAL_141_FILES_STILL_BYTE_IDENTICAL=1\n'
sha256sum --quiet -c "$staging/existing_runtime_before.SHA256SUMS"
printf 'PREEXISTING_16_FILES_STILL_BYTE_IDENTICAL=1\n'
release=$(readlink -f "$base/current")
printf 'DEPLOYED_RELEASE=%s\n' "${release##*/}"
sha256sum "$release/sn100_shell" "$release/sn100_report_renderer"
REMOTE
printf 'FORMAL_STEP=reopen_for_user\n'
SN100_RK_WORK_DIR=$work bash "$module/rk.sh" start 0 offlineAdmin > "$here/formal_reopen.log" 2>&1
SN100_RK_WORK_DIR=$work bash "$module/rk.sh" status > "$here/formal_reopen_status.log" 2>&1
printf 'FORMAL_ORIGINAL_DB_DEPLOY_AND_REOPEN_PASSED=1\n'
