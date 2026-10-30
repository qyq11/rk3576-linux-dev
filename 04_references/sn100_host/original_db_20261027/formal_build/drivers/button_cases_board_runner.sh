#!/usr/bin/env bash
# 每项沿用原QtTest业务断言，按正式关闭重开的方式使用新进程。
set -Eeuo pipefail
run=$1; mode=$2
[[ $run == /home/rkshare/programs/sn100_original/20261027_01/verification/page_* && $EUID != 0 ]] || exit 2
case $mode in
 connection) cases=(readsOriginalKeysAndSavesOnce invalidDraftDoesNotWrite hashCommentsAndUnrelatedBytesSurviveSave externalChangePreservesDraft nonAdministratorCannotSave) ;;
 shutdown) cases=(cancelAndRepeatedRequestsPreserveWindow confirmsOuterWindowClose pendingDataRejectsExit keepUnsavedDraftRejectsExit) ;;
 *) exit 2 ;;
esac
release=$(readlink -f /home/rkshare/programs/sn100_shell/current)
[[ $(sha256sum "$release/SHA256SUMS" | cut -d' ' -f1) == "${release##*/}" ]] || exit 3
(cd "$release" && sha256sum --quiet -c SHA256SUMS)
source "$release/rk_board.sh"
runtime_environment "$release" "$run/cache"
export LD_LIBRARY_PATH=$run/lib:$LD_LIBRARY_PATH
mkdir -p "$run/xdg"; chmod 700 "$run/xdg"
cat > "$run/kms.json" <<'KMS'
{"device":"/dev/dri/card0","hwcursor":false,"outputs":[{"name":"HDMI1","mode":"preferred","primary":true},{"name":"DP1","mode":"off"},{"name":"Writeback1","mode":"off"}]}
KMS
export XDG_RUNTIME_DIR=$run/xdg QT_QPA_EGLFS_KMS_CONFIG=$run/kms.json SN100_BUTTON_TEST_ROOT=$run/data
cd "$run"
printf 'ARCH=%s\nKERNEL=%s\nQPA=%s\nCASE_EXECUTION=new_process_per_case\n' "$(uname -m)" "$(uname -r)" "$QT_QPA_PLATFORM" > runtime.txt
printf 'CASE_EXECUTION=new_process_per_case; original assertions unchanged\n' > app.log
printf 'case\texit_code\n' > cases.tsv
printf 'case\tuptime_s\trss_kib\tpss_kib\tthreads\n' > resources.tsv
result=0
for test_case in "${cases[@]}"; do
    ./driver "$test_case" -v1 > "$test_case.log" 2>&1 &
    child=$!; start=$SECONDS
    printf '%s\n' "$child" > app.pid
    while kill -0 "$child" 2>/dev/null; do
        if [[ -r /proc/$child/status ]]; then
            read -r uptime _ < /proc/uptime
            rss=$(awk '$1=="VmRSS:" {print $2}' /proc/$child/status || true)
            pss=$(awk '$1=="Pss:" {print $2}' /proc/$child/smaps_rollup 2>/dev/null || true)
            threads=$(awk '$1=="Threads:" {print $2}' /proc/$child/status || true)
            printf '%s\t%s\t%s\t%s\t%s\n' "$test_case" "$uptime" "$rss" "$pss" "$threads" >> resources.tsv
        fi
        if ((SECONDS-start > 60)); then kill -TERM "$child" 2>/dev/null || true; fi
        sleep .2
    done
    set +e
    wait "$child"; code=$?
    set -e
    printf '%s\t%s\n' "$test_case" "$code" >> cases.tsv
    cat "$test_case.log" >> app.log
    if [[ $code != 0 ]]; then result=1; fi
done
printf '%s\n' "$result" > exit_code
printf 'ACTUAL_EXIT_CODE=%s\n' "$result" > result.txt
exit "$result"
