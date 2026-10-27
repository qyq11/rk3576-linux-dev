#!/usr/bin/env bash
set -Eeuo pipefail
umask 077
base=/home/rkshare/programs/sn100_shell
release=$(readlink -f "$base/current")
validation=$base/validation/20261027_01
[[ $release == "$base/releases/"* && -d $validation ]]
source "$release/rk_board.sh"
verify_release "$release"
runtime_environment "$release" "$validation/cache"
export QT_QPA_PLATFORM=offscreen XDG_RUNTIME_DIR=$validation/runtime
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
cd "$release"
child=; ticks=; secret=
cleanup()
{
    local result=$?
    trap - EXIT
    [[ -z $secret ]] || rm -f -- "$secret"
    if [[ -n $child && -n $ticks ]] && [[ $(start_ticks "$child" 2>/dev/null) == "$ticks" ]] \
        && [[ $(readlink /proc/"$child"/exe 2>/dev/null) == "$release/sn100_shell" ]]; then
        kill -TERM "$child" 2>/dev/null || true
        wait "$child" || true
    fi
    exit "$result"
}
trap cleanup EXIT
printf 'BOARD_SMOKE_START='; date -u +%FT%TZ
printf 'CLK_TCK='; getconf CLK_TCK
printf 'CPU_COUNT='; getconf _NPROCESSORS_ONLN
for phase in fresh reopen; do
    log=$validation/local_results_${phase}.log
    test ! -e "$log"
    seed=()
    secret=
    if [[ $phase == fresh ]]; then
        test ! -e "$validation/local_results_data"
        secret=$(mktemp "$validation/runtime/bootstrap.XXXXXXXX")
        cat /proc/sys/kernel/random/uuid > "$secret"
        seed=(--seed-anonymous --test-secret-file "$secret")
    fi
    "$release/sn100_shell" --offline --accounts --password-login --history --local-results --catalog --samples --instrument --printer-settings --lis-settings --lis-raw-inbox --quality-control --calibration --alarm-history --test-session offlineAdmin --data-dir "$validation/local_results_data" --viewport 1600x900 "${seed[@]}" > "$log" 2>&1 &
    child=$!
    ticks=$(start_ticks "$child")
    for ((n=0; n<150; n++)); do
        grep -qx SHELL_READY "$log" && break
        kill -0 "$child" 2>/dev/null || break
        sleep 0.2
    done
    [[ -z $secret ]] || rm -f -- "$secret"
    grep -qx SHELL_READY "$log" || { tail -35 "$log"; printf 'STARTUP_FAILED=%s\n' "$phase"; exit 1; }
    [[ $(start_ticks "$child") == "$ticks" && $(readlink /proc/"$child"/exe) == "$release/sn100_shell" ]]
    kill -TERM "$child"
    set +e
    wait "$child"
    code=$?
    set -e
    printf '%s\n' "$code" > "$validation/local_results_${phase}.exit"
    printf 'LOCAL_RESULTS_%s_EXIT=%s\n' "$phase" "$code"
    tail -22 "$log"
    [[ $code == 0 ]]
    grep -qx 'THREADS_STOPPED true' "$log"
    child=; ticks=; secret=
done
timeout 30 "$release/sn100_report_renderer" "$validation/report_snapshot.json" "$validation/report.pdf" > "$validation/pdf.log" 2>&1
printf 'REPORT_PDF_EXIT=0\n'
timeout 30 "$release/sn100_report_renderer" --html "$validation/report_snapshot.json" "$validation/report.html" > "$validation/html.log" 2>&1
printf 'REPORT_HTML_EXIT=0\n'
printf 'PDF_SIGNATURE='; head -c 5 "$validation/report.pdf"; printf '\n'
sha256sum "$validation/report.pdf" "$validation/report.html"
printf 'BOARD_SMOKE_COMPLETE=1\n'
