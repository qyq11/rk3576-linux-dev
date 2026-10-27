#!/usr/bin/env bash
# Execute exactly one coordinated GUI invocation. Never called by build_drivers.py.
set -Eeuo pipefail
[[ $EUID != 0 ]] || { echo 'Run as ordinary WSL user qyq.' >&2; exit 2; }
cumulative_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
work_dir=$(cd -- "$cumulative_dir/.." && pwd)
qpa=${1:?Usage: run_case.sh offscreen|xcb integrated <method> | status}
suite=${2:?Specify integrated or status}
[[ $qpa == offscreen || $qpa == xcb ]] || { echo 'Unsupported QPA.' >&2; exit 2; }
[[ -f $cumulative_dir/build_result.json ]] || { echo 'Build both drivers first.' >&2; exit 2; }
mkdir -p "$cumulative_dir/logs/$qpa"
export QT_QPA_PLATFORM=$qpa
unset QMAKESPEC QMAKEPATH QMAKEFEATURES
case $suite in
    integrated)
        method=${3:?Choose one integrated method; write/read need separate processes}
        case $method in
            writeExperiment|readExperiment|initializationFailureExperiment) ;;
            viewportExperiment)
                [[ $qpa == xcb ]] || { echo 'viewportExperiment requires XCB screen geometry and native widget hit testing; offscreen is not applicable.' >&2; exit 2; }
                ;;
            applicationExitExperiment)
                [[ $qpa == xcb ]] || { echo 'applicationExitExperiment requires XCB.' >&2; exit 2; }
                command -v xdotool >/dev/null || { echo 'xdotool is required.' >&2; exit 2; }
                ;;
            *) echo 'Unsupported method; the 60-second measurement is excluded this round.' >&2; exit 2 ;;
        esac
        export SN100_INTEGRATION_EVIDENCE_DIR=$cumulative_dir/runtime/$qpa/integrated
        export SN100_SHELL_APP=$work_dir/native/wsl_release/sn100_shell
        prefix=$cumulative_dir/logs/$qpa/integrated_$method
        binary=$cumulative_dir/integrated_shell_test/integrated_shell_test
        arguments=("$method")
        ;;
    status)
        [[ $# == 2 ]] || { echo 'status runs the whole suite; do not add a method.' >&2; exit 2; }
        export SN100_STATUS_REPLAY_EVIDENCE_DIR=$cumulative_dir/runtime/$qpa/status_replay
        [[ ! -e $SN100_STATUS_REPLAY_EVIDENCE_DIR ]] || { echo 'Status evidence root must be new.' >&2; exit 2; }
        export SN100_STATUS_REPLAY_FIXTURE=$work_dir/source/06_programs/sn100_host/tests/fixtures/status_replay.json
        export SN100_STATUS_REPLAY_APP=$work_dir/native/wsl_release/sn100_shell
        prefix=$cumulative_dir/logs/$qpa/status_replay
        binary=$cumulative_dir/status_replay_shell_test/status_replay_shell_test
        arguments=()
        ;;
    *) echo 'Unknown suite.' >&2; exit 2 ;;
esac
[[ ! -e $prefix.txt && ! -e $prefix.xml ]] || { echo 'Do not overwrite earlier evidence.' >&2; exit 2; }
exec "$cumulative_dir/target_wrapper.sh" "$binary" "${arguments[@]}" -o "$prefix.txt",txt -o "$prefix.xml",xml
