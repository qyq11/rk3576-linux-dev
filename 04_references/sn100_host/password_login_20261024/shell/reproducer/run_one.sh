#!/usr/bin/env bash
set -Eeuo pipefail
[[ $# == 2 && ( $1 == offscreen || $1 == xcb ) && $2 =~ ^run_[0-9]+$ ]]
root=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
work=$root/temp/sn100_host/o02_login/shell_test
output=$work/$2/$1
[[ ! -e $output ]] || { echo 'Refuse to reuse prior test output.' >&2; exit 2; }
mkdir -p "$output"
export SN100_LOGIN_EVIDENCE_DIR=$output/evidence
export SN100_LOGIN_RUNTIME_DIR=$output/runtime
export SN100_SHELL_APP=$root/temp/sn100_host/o02_login/native/wsl_release/sn100_shell
export SN100_SHELL_SHA256=2ce1df6cb0be38085e1e5834fd0037a43bd906c6e87d451a06e4e25cbbfc61c9
export QT_QPA_PLATFORM=$1
export QT_SCALE_FACTOR=1
export QT_AUTO_SCREEN_SCALE_FACTOR=0
export SN100_WSL_IME=off
{
    date -u +'%Y-%m-%dT%H:%M:%SZ'
    id
    printf 'QT_QPA_PLATFORM=%s\nQT_SCALE_FACTOR=%s\n' "$QT_QPA_PLATFORM" "$QT_SCALE_FACTOR"
    sha256sum "$SN100_SHELL_APP" "$work/production_test_driver/password_login_shell_test"
} > "$output/environment.txt"
set +e
timeout 85s "$work/production_test_driver/password_login_shell_test" -o "$output/qt.log",txt \
    > "$output/console.log" 2>&1
result=$?
set -e
printf '%s\n' "$result" > "$output/exit_code.txt"
printf 'LOGIN_SHELL_TEST_PLATFORM=%s EXIT=%s OUTPUT=%s\n' "$QT_QPA_PLATFORM" "$result" "$output"
exit "$result"
