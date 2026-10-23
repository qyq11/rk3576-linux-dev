#!/usr/bin/env bash
# Build once from a frozen public-source snapshot; run only after an explicit
# GUI test slot is available. Product layout/animation and other entrypoints stay
# unchanged. No credentials are passed on the command line or copied to evidence.
set -Eeuo pipefail
[[ $EUID != 0 ]] || { echo 'Use the ordinary WSL user qyq.' >&2; exit 2; }
tests_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
program_dir=$(cd -- "$tests_dir/.." && pwd)
root=$(cd -- "$program_dir/../.." && pwd)
base_work=$root/temp/sn100_host/o03_2_latency
work=${SN100_LATENCY_WORK_DIR:-$base_work}
# Check the uncreated path first. A failed immutable snapshot can be retained
# while a corrected attempt gets its own complete source/build/data directory.
[[ $work == /* && $work == "$(realpath -m -- "$work")" ]] || { echo 'SN100_LATENCY_WORK_DIR must be a canonical absolute path without symlinks or dot components.' >&2; exit 2; }
if [[ $work != "$base_work" ]]; then
    [[ $work == "$base_work"/attempts/* ]] || { echo 'Latency work must be the default directory or attempts/<simple-id> beneath it.' >&2; exit 2; }
    attempt_id=${work#"$base_work"/attempts/}
    [[ $attempt_id =~ ^[a-z0-9_-]+$ ]] || { echo 'Attempt id must contain only lowercase letters, digits, underscore or hyphen; nested paths are refused.' >&2; exit 2; }
fi
checked_path=$work
while [[ $checked_path != / ]]; do
    [[ ! -L $checked_path ]] || { echo 'Latency work cannot traverse a symlink.' >&2; exit 2; }
    checked_path=$(dirname -- "$checked_path")
done
build_dir=$work/build
generated=$work/generated
snapshot=$work/source_snapshot
prepare=$tests_dir/prepare_shell_latency.py
qmake=/usr/lib/qt5/bin/qmake
mode=${1:-}
[[ $# == 0 ]] || shift
[[ $(uname -m) == x86_64 && -x $qmake ]] || { echo 'WSL x86_64 native Qt5 is required.' >&2; exit 2; }
unset QMAKESPEC QMAKEPATH QMAKEFEATURES QT_PLUGIN_PATH QML2_IMPORT_PATH
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
mkdir -p "$work/logs"
case $mode in
    build)
        [[ $# == 0 ]] || { echo 'build does not take test arguments.' >&2; exit 2; }
        # Never overwrite a successful frozen target: subsequent repeats use
        # exactly the source and executable whose hashes were recorded.
        if [[ -f $work/build_metadata.json ]]; then
            python3 "$prepare" verify-built --program-dir "$program_dir" --work-dir "$work"
            echo "Reusing successful frozen executable: $build_dir/shell_latency_test"
            exit 0
        fi
        mkdir "$work/.build_lock" || { echo 'Another latency build is active; inspect the dedicated lock before retry.' >&2; exit 2; }
        trap 'rmdir -- "$work/.build_lock"' EXIT
        python3 "$prepare" prepare --program-dir "$program_dir" --work-dir "$work"
        mkdir -p "$build_dir"
        # A failed previous build has no success metadata and may leave output.
        # Remove only this known executable before building; never run leftovers.
        rm -f -- "$build_dir/shell_latency_test"
        jobs=${SN100_JOBS:-4}
        [[ $jobs =~ ^[1-9][0-9]*$ && $jobs -le 16 ]] || { echo 'SN100_JOBS must be 1..16.' >&2; exit 2; }
        log=$work/logs/build_$(date -u +%Y%m%dT%H%M%SZ).log
        (
            cd "$build_dir"
            "$qmake" "$snapshot/tests/shell_latency_test.pro" -spec linux-g++ \
                CONFIG+=release CONFIG-=debug "SN100_LATENCY_GENERATED=$generated"
            make -j"$jobs"
            file ./shell_latency_test
        ) 2>&1 | tee "$log"
        python3 "$prepare" mark-built --program-dir "$program_dir" --work-dir "$work" \
            --binary "$build_dir/shell_latency_test"
        echo "Built frozen latency test; coordinate a GUI slot before run. Log: $log"
        ;;
    run)
        [[ $# -gt 0 ]] || { echo 'Usage: shell_latency.sh run SLOT [SLOT ...] [QtTest arguments]' >&2; exit 2; }
        [[ -n ${SN100_LATENCY_EVIDENCE_DIR:-} ]] || { echo 'Set SN100_LATENCY_EVIDENCE_DIR to a new absolute directory under the latency work root.' >&2; exit 2; }
        python3 "$prepare" prepare-run --program-dir "$program_dir" --work-dir "$work" \
            --evidence-dir "$SN100_LATENCY_EVIDENCE_DIR"
        # No implicit build and no reuse of earlier account/hospital runtime.
        cd "$build_dir"
        exec env QT_QPA_PLATFORM=xcb "$build_dir/shell_latency_test" "$@"
        ;;
    *) echo 'Usage: bash tests/shell_latency.sh {build|run SLOT...}' >&2; exit 2 ;;
esac
