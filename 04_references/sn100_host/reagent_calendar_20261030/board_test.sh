#!/usr/bin/env bash
# 仅在新匿名目录运行同批驱动，记录实际退出、资源和输出；不接真实仪器。
set -Eeuo pipefail
run=$1; mode=$2; scenario=${3:-}
[[ $run =~ ^/tmp/sn100-calendar-20261030\.[a-zA-Z0-9]{6}$ && $(readlink -f "$run") == "$run" && $EUID != 0 ]]
exec 9>/home/rkshare/programs/sn100_shell/lifecycle.lock
flock -n 9
release=$run/$(cat "$run/release_name")
source "$release/rk_board.sh"
verify_release "$release"
runtime_environment "$release" "$run/cache"
export LD_LIBRARY_PATH=$run:$LD_LIBRARY_PATH
export QT_QPA_PLATFORM=$mode
mkdir "$run/data" "$run/output" "$run/xdg"
chmod 700 "$run/xdg"
export XDG_RUNTIME_DIR=$run/xdg SN100_BUTTON_TEST_ROOT=$run/data SN100_FORMAL_MAIN_BINARY=$release/sn100_shell SN100_GUI_EVIDENCE_DIR=$run/output
export SN100_KEEP_ANONYMOUS_FIXTURE=1
export SN100_HISTORY_TEST_ROOT=$run/data SN100_HISTORY_OUTPUT_DIR=$run/output SN100_FORMAL_REPORT_RENDERER=$release/sn100_report_renderer
if [[ -f $run/original_report.xml ]]; then export SN100_ORIGINAL_REPORT_XML=$run/original_report.xml; fi
if [[ $mode == eglfs ]]; then
    cat > "$run/kms.json" <<'KMS'
{"device":"/dev/dri/card0","hwcursor":false,"outputs":[{"name":"HDMI1","mode":"preferred","primary":true},{"name":"DP1","mode":"off"},{"name":"Writeback1","mode":"off"}]}
KMS
    export QT_QPA_EGLFS_KMS_CONFIG=$run/kms.json
fi
cd "$run"
{ date -Is; uname -a; id; getconf CLK_TCK; printf 'QPA=%s\nREAL_INSTRUMENT=0\nANONYMOUS_ONLY=1\nCURRENT_BEFORE=%s\n' "$mode" "$(readlink -f /home/rkshare/programs/sn100_shell/current)"; sha256sum driver "$release/sn100_shell"; } > runtime.txt
LD_LIBRARY_PATH=$run:$release/lib ldd driver > dependencies.txt
LD_LIBRARY_PATH=$run:$release/lib ldd "$release/sn100_shell" >> dependencies.txt
LD_LIBRARY_PATH=$run:$release/lib ldd "$release/sn100_report_renderer" >> dependencies.txt
! grep -q 'not found' dependencies.txt
cases=()
case "$scenario" in
    ''|all) ;;
    quality_pages) cases=(originalPagesReadRealIdsAndFields qualityCancelSaveAndWriteLockRecovery qualityExternalConflictKeepsDraftAndRecovers calibrationCancelSaveAndExternalConflict);;
    batch) cases=(formalHoleBatchChoicesFollowOriginalProjects);;
    task_pages) cases=(formalHolePageSaveWaitsForMatchingDeviceReadback formalNormalHoleSaveCancelAndReopen formalHoleBatchChoicesFollowOriginalProjects formalHoleReagentChoicesFollowOriginalAreas formalStartPauseResumeTasksAndRawResult);;
    *) exit 2;;
esac
printf 'uptime_s\tuser_ticks\tsystem_ticks\trss_kib\tpss_kib\tthreads\tfd_count\ttemperatures_mC\n' > resources.tsv
timeout -k 5 150 ./driver "${cases[@]}" -v1 > app.log 2>&1 &
supervisor=$!
while kill -0 "$supervisor" 2>/dev/null; do
    child=$(pgrep -P "$supervisor" -x driver | head -1 || true)
    if [[ -n $child && -r /proc/$child/stat ]]; then
        stat=$(cat "/proc/$child/stat" 2>/dev/null || true); rest=${stat##*) }; set -- $rest
        rss=$(awk '$1=="VmRSS:" {print $2}' "/proc/$child/status" 2>/dev/null || true)
        pss=$(awk '$1=="Pss:" {print $2}' "/proc/$child/smaps_rollup" 2>/dev/null || true)
        threads=$(awk '$1=="Threads:" {print $2}' "/proc/$child/status" 2>/dev/null || true)
        fds=$(find "/proc/$child/fd" -maxdepth 1 -type l 2>/dev/null | wc -l || true)
        read -r uptime _ < /proc/uptime
        temperatures=
        for node in /sys/class/thermal/thermal_zone*/temp; do temperatures+="$(cat "$node");"; done
        printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$uptime" "${12:-NA}" "${13:-NA}" "${rss:-NA}" "${pss:-NA}" "${threads:-NA}" "$fds" "$temperatures" >> resources.tsv
    fi
    sleep .25
done
set +e
wait "$supervisor"; code=$?
set -e
printf '%s\n' "$code" > exit_code
printf 'ACTUAL_EXIT_CODE=%s\nCURRENT_AFTER=%s\n' "$code" "$(readlink -f /home/rkshare/programs/sn100_shell/current)" >> runtime.txt
du -sb data output app.log >> runtime.txt
exit "$code"
