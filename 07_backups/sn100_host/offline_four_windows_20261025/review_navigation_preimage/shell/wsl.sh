#!/usr/bin/env bash
# Preserve O03 modes; integrated modes own separate build/test/runtime paths.
set -Eeuo pipefail
[[ $EUID != 0 ]] || { echo 'Build/run as the ordinary WSL user qyq.' >&2; exit 2; }
module_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$module_dir/../../.." && pwd)
qmake=/usr/lib/qt5/bin/qmake
mode=${1:-run}
[[ $# == 0 ]] || shift
case $mode in
    integrated-build|integrated-run|integrated-test) work=$root/temp/sn100_host/integration ;;
    *) work=$root/temp/sn100_host/o03_2 ;;
esac
work=${SN100_SHELL_WORK_DIR:-$work}
[[ $work == /* ]] || { echo 'SN100_SHELL_WORK_DIR must be absolute.' >&2; exit 2; }
[[ $(uname -m) == x86_64 && -x $qmake ]] || { echo 'Native WSL Qt5 is required.' >&2; exit 1; }
unset QMAKESPEC QMAKEPATH QMAKEFEATURES QT_PLUGIN_PATH QML2_IMPORT_PATH
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
build_dir=$work/wsl_release
renderer_build_dir=$work/wsl_renderer_release
stage_dir=$work/wsl_report_stage
mkdir -p "$build_dir" "$work/logs"
build()
{
    mkdir -p "$renderer_build_dir" "$stage_dir"
    (
        flock -n 9 || { echo 'This native shell build is already in use.' >&2; exit 1; }
        (cd "$renderer_build_dir" && "$qmake" "$module_dir/../history/renderer.pro" -spec linux-g++ CONFIG+=release CONFIG-=debug "DESTDIR=$stage_dir" && make -j"${SN100_JOBS:-4}")
        (cd "$build_dir" && "$qmake" "$module_dir/shell.pro" -spec linux-g++ CONFIG+=release CONFIG-=debug "DESTDIR=$stage_dir" && make -j"${SN100_JOBS:-4}")
        [[ -x $stage_dir/sn100_shell && -x $stage_dir/sn100_report_renderer ]]
        mv -f -- "$stage_dir/sn100_report_renderer" "$build_dir/sn100_report_renderer"
        mv -f -- "$stage_dir/sn100_shell" "$build_dir/sn100_shell"
    ) 9>"$work/native_build.lock"
    file "$build_dir/sn100_shell" "$build_dir/sn100_report_renderer"
    sha256sum "$build_dir/sn100_shell" "$build_dir/sn100_report_renderer"
}
case $mode in
    build|integrated-build) build ;;
    run|integrated-run)
        build
        runtime=$work/runtime
        arguments=()
        supplied_seed=0
        while [[ $# -gt 0 ]]; do
            case $1 in
                --data-dir) [[ $# -ge 2 ]] || { echo '--data-dir needs a value.' >&2; exit 2; }; runtime=$2; shift 2 ;;
                --data-dir=*) runtime=${1#*=}; shift ;;
                --seed-anonymous|--test-secret-file|--test-secret-file=*)
                    supplied_seed=1; arguments+=("$1"); shift ;;
                *) arguments+=("$1"); shift ;;
            esac
        done
        [[ $runtime == /* ]] || { echo '--data-dir must be absolute.' >&2; exit 2; }
        seed=()
        secret_file=
        cleanup() { if [[ -n $secret_file ]]; then rm -f -- "$secret_file"; fi; }
        trap cleanup EXIT
        trap 'exit 130' INT
        trap 'exit 143' TERM
        if [[ $supplied_seed == 0 && ! -f $runtime/account/user_Information.db ]]; then
            # Only the transient file contains the bootstrap secret. Keep it
            # outside public evidence and remove it after the process exits.
            umask 077
            secret_file=$(mktemp "$work/account_bootstrap.XXXXXX")
            cat /proc/sys/kernel/random/uuid > "$secret_file"
            seed=(--seed-anonymous --test-secret-file "$secret_file")
        fi
        features=(--accounts)
        if [[ $mode == integrated-run ]]; then features+=(--history --catalog --samples --instrument --password-login --printer-settings --lis-settings --quality-control --calibration); fi
        cd "$build_dir"
        # No implicit test-session: Guest remains the default. An explicit
        # --test-session engineer is passed through as an offline simulation.
        bash "$module_dir/../wsl_ime.sh" env QT_QPA_PLATFORM=xcb "$build_dir/sn100_shell" --offline "${features[@]}" --data-dir "$runtime" "${seed[@]}" "${arguments[@]}"
        ;;
    integrated-test)
        [[ $# == 0 ]] || { echo 'integrated-test builds only; run the resulting test separately with its arguments.' >&2; exit 2; }
        build
        mkdir -p "$work/wsl_tests"
        cd "$work/wsl_tests"
        "$qmake" "$module_dir/../tests/integrated_shell_test.pro" -spec linux-g++ CONFIG+=release CONFIG-=debug
        make -j"${SN100_JOBS:-4}"
        file "$work/wsl_tests/integrated_shell_test"
        echo "Built $work/wsl_tests/integrated_shell_test; GUI execution requires its separate coordinated slot."
        ;;
    test)
        build
        mkdir -p "$work/wsl_tests"
        cd "$work/wsl_tests"
        "$qmake" "$module_dir/../tests/shell_test.pro" -spec linux-g++ CONFIG+=release CONFIG-=debug
        make -j"${SN100_JOBS:-4}"
        exec env QT_QPA_PLATFORM=xcb SN100_SHELL_APP="$build_dir/sn100_shell" ./shell_test "$@"
        ;;
    account-test)
        build
        mkdir -p "$work/wsl_account_tests"
        cd "$work/wsl_account_tests"
        "$qmake" "$module_dir/../tests/shell_account_test.pro" -spec linux-g++ CONFIG+=release CONFIG-=debug
        make -j"${SN100_JOBS:-4}"
        exec env QT_QPA_PLATFORM=xcb SN100_SHELL_APP="$build_dir/sn100_shell" ./shell_account_test "$@"
        ;;
    hospital-test)
        mkdir -p "$work/hospital_regression"
        cd "$work/hospital_regression"
        "$qmake" "$module_dir/../tests/offline_test.pro" -spec linux-g++ CONFIG+=release CONFIG-=debug
        make -j"${SN100_JOBS:-4}"
        exec env QT_QPA_PLATFORM=xcb ./offline_test "$@"
        ;;
    *) echo 'Usage: bash shell/wsl.sh {build|run|test|account-test|hospital-test|integrated-build|integrated-run|integrated-test} [arguments]' >&2; exit 2 ;;
esac
