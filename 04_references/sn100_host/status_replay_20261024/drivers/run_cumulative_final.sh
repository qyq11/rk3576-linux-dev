#!/usr/bin/env bash
set -u
root=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
out=$root/temp/sn100_host/status_replay_20261024
export SN100_SHELL_APP=$out/native/wsl_release/sn100_shell
export SN100_REPORT_RENDERER=$out/native/wsl_release/sn100_report_renderer
export SN100_REPORT_PDF_PYTHON=/mnt/c/Users/52400/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe
run_case() {
    local label=$1; shift
    timeout 150 "$@" -o "$out/logs/${label}.txt",txt
    local code=$?
    printf '%s\t%s\n' "$label" "$code" >> "$out/logs/cumulative_exits.tsv"
    tail -n 7 "$out/logs/${label}.txt"
    [[ $code == 0 ]] || exit "$code"
}
for qpa in offscreen xcb; do
    export QT_QPA_PLATFORM=$qpa
    export SN100_CATALOG_EVIDENCE_DIR=$out/cumulative_catalog_$qpa
    run_case catalog_$qpa "$out/shell_test/catalog_shell_test" referencesAndGuards productStartupFailure unknownInitialActorFails samplePresetWorkflow fullProductStartupAndShutdown
done
export QT_QPA_PLATFORM=xcb
export SN100_INTEGRATION_EVIDENCE_DIR=$out/cumulative_core
for name in writeExperiment readExperiment applicationExitExperiment viewportExperiment initializationFailureExperiment; do
    run_case cumulative_$name "$out/shell_test/integrated_shell_test" "$name"
done
export SN100_INTEGRATED_REPORT_EVIDENCE_DIR=$out/cumulative_report
run_case cumulative_report "$out/shell_test/integrated_report_test" committedHospitalAndReportLifecycle persistedHospitalAndLegacyEntry
