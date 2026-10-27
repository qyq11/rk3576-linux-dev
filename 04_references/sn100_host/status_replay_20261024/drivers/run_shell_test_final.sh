#!/usr/bin/env bash
set -u
root=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
out=$root/temp/sn100_host/status_replay_20261024
[[ $(cat "$out/logs/native_final.exit") == 0 ]] || exit 1
export SN100_STATUS_REPLAY_FIXTURE=$root/06_programs/sn100_host/tests/fixtures/status_replay.json
export SN100_STATUS_REPLAY_APP=$out/native/wsl_release/sn100_shell
sha256sum "$SN100_STATUS_REPLAY_APP" "$out/shell_test/status_replay_shell_test" "$SN100_STATUS_REPLAY_FIXTURE" > "$out/logs/gui_final_inputs_before.sha256"
for qpa in offscreen xcb; do
    export QT_QPA_PLATFORM=$qpa
    export SN100_STATUS_REPLAY_EVIDENCE_DIR=$out/gui_${qpa}_05
    timeout 90 "$out/shell_test/status_replay_shell_test" -o "$out/logs/gui_${qpa}_05.txt",txt
    code=$?
    printf '%s\t%s\n' "$qpa" "$code" >> "$out/logs/gui_exits_05.tsv"
    tail -n 7 "$out/logs/gui_${qpa}_05.txt"
    [[ $code == 0 ]] || exit "$code"
done
sha256sum "$SN100_STATUS_REPLAY_APP" "$out/shell_test/status_replay_shell_test" "$SN100_STATUS_REPLAY_FIXTURE" > "$out/logs/gui_final_inputs_after.sha256"
cmp "$out/logs/gui_final_inputs_before.sha256" "$out/logs/gui_final_inputs_after.sha256"
