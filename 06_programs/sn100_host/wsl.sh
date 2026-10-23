#!/usr/bin/env bash
# Invoke inside Ubuntu-24.04 as qyq. Explicitly builds native x86_64, not ARM64.
set -Eeuo pipefail
[[ $EUID != 0 ]] || { echo 'Build/run as the ordinary WSL user qyq.' >&2; exit 2; }
program_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$program_dir/../.." && pwd)
work=$root/temp/sn100_host
qmake=/usr/lib/qt5/bin/qmake
mode=${1:-run}
[[ $# == 0 ]] || shift
[[ $(uname -m) == x86_64 && -x $qmake ]] || { echo 'Native WSL Qt5 is missing; see setup_wsl.sh.' >&2; exit 1; }
unset QMAKESPEC QMAKEPATH QMAKEFEATURES QT_PLUGIN_PATH QML2_IMPORT_PATH
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
build_dir=$work/wsl_release
mkdir -p "$build_dir" "$work/logs"
build()
{
    (cd "$build_dir" && "$qmake" "$program_dir/sn100_host.pro" -spec linux-g++ CONFIG+=release CONFIG-=debug && make -j"${SN100_JOBS:-4}")
    file "$build_dir/sn100_host"
}
setup_ide()
{
    local settings=${XDG_CONFIG_HOME:-$HOME/.config}/QtProject/qtcreator
    local sdktool=/usr/libexec/qtcreator/sdktool
    mkdir -p "$settings"
    if ! grep -qs 'sn100.qt5.native' "$settings/qtversion.xml"; then
        "$sdktool" -s "$settings" addQt --id sn100.qt5.native --name 'Qt 5.15.13 WSL x86_64' --qmake "$qmake" --type Qt4ProjectManager.QtVersion.Desktop
    fi
    if ! grep -qs 'ProjectExplorer.ToolChain.Gcc:sn100.c<' "$settings/toolchains.xml"; then
        "$sdktool" -s "$settings" addTC --id ProjectExplorer.ToolChain.Gcc:sn100.c --language C --name 'GCC 13 WSL C' --path /usr/bin/gcc --abi x86-linux-generic-elf-64bit
    fi
    if ! grep -qs 'ProjectExplorer.ToolChain.Gcc:sn100.cxx<' "$settings/toolchains.xml"; then
        "$sdktool" -s "$settings" addTC --id ProjectExplorer.ToolChain.Gcc:sn100.cxx --language Cxx --name 'G++ 13 WSL C++' --path /usr/bin/g++ --abi x86-linux-generic-elf-64bit
    fi
    if ! grep -qs 'sn100.gdb' "$settings/debuggers.xml"; then
        "$sdktool" -s "$settings" addDebugger --id sn100.gdb --name 'GDB WSL x86_64' --engine 1 --binary /usr/bin/gdb --abis x86-linux-generic-elf-64bit
    fi
    if ! grep -qs 'sn100.native.qt5' "$settings/profiles.xml"; then
        "$sdktool" -s "$settings" addKit --id sn100.native.qt5 --name 'SN100 WSL Qt 5.15.13 x86_64' --debuggerid sn100.gdb --devicetype Desktop --Ctoolchain ProjectExplorer.ToolChain.Gcc:sn100.c --Cxxtoolchain ProjectExplorer.ToolChain.Gcc:sn100.cxx --qt sn100.qt5.native --mkspec linux-g++
    fi
    echo "Configured native Qt5 kit: $settings"
}
case $mode in
    build) build ;;
    run)
        build
        cd "$build_dir"
        exec bash "$program_dir/wsl_ime.sh" env QT_QPA_PLATFORM=xcb "$build_dir/sn100_host" --offline --data-dir "$work/runtime" "$@"
        ;;
    account)
        build
        account_dir=$work/runtime_account
        account_args=()
        while (( $# )); do
            case $1 in
                --data-dir)
                    [[ $# -ge 2 ]] || { echo '--data-dir requires an absolute directory.' >&2; exit 2; }
                    account_dir=$2
                    shift 2
                    ;;
                --data-dir=*) account_dir=${1#*=}; shift ;;
                *) account_args+=("$1"); shift ;;
            esac
        done
        [[ $account_dir == /* ]] || { echo '--data-dir requires an absolute directory.' >&2; exit 2; }
        seed=()
        if [[ ! -f $account_dir/user_Information.db ]]; then
            umask 077
            # Ephemeral secret is never printed or stored in the source/evidence.
            # Keep this in temp even if the requested data path is rejected by
            # AccountStore. Do not mkdir a user path before its canonical checks.
            if [[ ! -f $work/account_bootstrap.secret ]]; then
                cat /proc/sys/kernel/random/uuid > "$work/account_bootstrap.secret"
            fi
            seed=(--seed-anonymous --test-secret-file "$work/account_bootstrap.secret")
        fi
        exec bash "$program_dir/wsl_ime.sh" env QT_QPA_PLATFORM=xcb "$build_dir/sn100_host" --offline --module account \
            --data-dir "$account_dir" --test-session offlineEngineer --device-state ready "${seed[@]}" "${account_args[@]}"
        ;;
    account-test)
        build
        mkdir -p "$work/wsl_account_tests"
        cd "$work/wsl_account_tests"
        "$qmake" "$program_dir/tests/account_test.pro" -spec linux-g++ CONFIG+=release CONFIG-=debug
        make -j"${SN100_JOBS:-4}"
        exec env QT_QPA_PLATFORM=xcb ./account_test "$@"
        ;;
    account-boundary-test)
        mkdir -p "$work/wsl_account_boundary_tests"
        cd "$work/wsl_account_boundary_tests"
        "$qmake" "$program_dir/tests/account_boundary_test.pro" -spec linux-g++ CONFIG+=release CONFIG-=debug
        make -j"${SN100_JOBS:-4}"
        exec ./account_boundary_test "$@"
        ;;
    test)
        build
        mkdir -p "$work/wsl_tests"
        cd "$work/wsl_tests"
        "$qmake" "$program_dir/tests/offline_test.pro" -spec linux-g++ CONFIG+=release CONFIG-=debug
        make -j"${SN100_JOBS:-4}"
        exec env QT_QPA_PLATFORM=xcb SN100_APP="$build_dir/sn100_host" ./offline_test "$@"
        ;;
    ide)
        setup_ide
        exec bash "$program_dir/wsl_ime.sh" env QT_QPA_PLATFORM=xcb qtcreator "$program_dir/sn100_host.pro"
        ;;
    setup-ide) setup_ide ;;
    *) echo 'Usage: bash wsl.sh {build|run|account|test|account-test|account-boundary-test|ide|setup-ide} [arguments]' >&2; exit 2 ;;
esac
