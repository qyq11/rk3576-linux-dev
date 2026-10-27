#!/usr/bin/env bash
set -Eeuo pipefail
[[ $# == 4 ]] || { echo 'Usage: run.sh {offscreen|xcb} unique_label absolute_final_app expected_sha256' >&2; exit 2; }
mode=$1
label=$2
application=$3
expected=$4
[[ $mode == offscreen || $mode == xcb ]] || exit 2
[[ $label =~ ^[A-Za-z0-9_-]{1,64}$ ]] || exit 2
[[ $application == /* && $expected =~ ^[a-f0-9]{64}$ ]] || exit 2
root=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
work="$root/temp/sn100_host/o09_loopback/shell_test"
evidence="$work/evidence/$label"
runtime="$work/runtime/$label"
[[ ! -e $evidence && ! -e $runtime && ! -e "$work/run_logs/$label.txt" ]] || exit 2
mkdir -p "$work/run_logs" "$work/evidence" "$work/runtime"
unset QMAKESPEC QMAKEPATH QMAKEFEATURES QT_PLUGIN_PATH QML2_IMPORT_PATH
set +e
env QT_QPA_PLATFORM="$mode" SN100_LOOPBACK_APP="$application" SN100_LOOPBACK_APP_SHA256="$expected" \
    SN100_LOOPBACK_EVIDENCE_DIR="$evidence" SN100_LOOPBACK_TEST_ROOT="$runtime" \
    SN100_LOOPBACK_REPLAY_FIXTURE="$root/06_programs/sn100_host/tests/fixtures/status_replay.json" \
    "$work/production_test_driver/status_loopback_shell_test" \
    -o "$work/run_logs/$label.txt,txt" -o "$work/run_logs/$label.xml,xml"
result=$?
set -e
if [[ -d $evidence ]]; then
    cp "$work/run_logs/$label.txt" "$work/run_logs/$label.xml" "$evidence/"
fi
exit "$result"
