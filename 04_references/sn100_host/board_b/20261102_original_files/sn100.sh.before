#!/usr/bin/env bash
# Install as /home/rkshare/programs/sn100_shell/sn100.sh on RK.
# Command-only entry: no autostart, rebuild, system shutdown or instrument I/O.
set -Eeuo pipefail
umask 077

usage()
{
    printf 'Usage: bash sn100.sh {start|stop|status}\n'
    printf 'start: 打开SN100（匿名测试管理员，无自动关闭时间）\n'
    printf 'stop: 正常关闭SN100，保留已保存数据\n'
    printf 'status: 查看程序状态\n'
}

[[ $# == 1 ]] || { usage >&2; exit 2; }
action=$1
case $action in
    -h|--help|help) usage; exit 0 ;;
    start|stop|status) ;;
    *) usage >&2; exit 2 ;;
esac
[[ $EUID != 0 && $(uname -m) == aarch64 ]] || {
    printf '请在RK板子上用lckfb普通用户执行，不要加sudo。\n' >&2
    exit 2
}
base=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
[[ $base != / && $base =~ ^/[a-zA-Z0-9_./-]+$ ]] || exit 2
release=$(readlink -f -- "$base/current")
[[ $release == "$base/releases/"* && ${release#"$base/releases/"} =~ ^[a-f0-9]{64}$ ]]
[[ -f $release/release_id && ! -L $release/release_id && -f $release/SHA256SUMS && ! -L $release/SHA256SUMS ]]
[[ $(cat "$release/release_id") == "${release##*/}" ]]
[[ $(sha256sum "$release/SHA256SUMS" | cut -d' ' -f1) == "${release##*/}" ]]
(cd -- "$release" && sha256sum --quiet -c SHA256SUMS)

if [[ $action == status && ! -f $base/latest_run ]]; then
    printf 'SN100尚未启动。\n'
    exit 0
fi
[[ ! -L $base/control_logs ]]
mkdir -p -- "$base/control_logs"
log=$(mktemp "$base/control_logs/${action}_$(date +%Y%m%d_%H%M%S).XXXXXXXX.log")
arguments=("$action" "$base")
[[ $action != start ]] || arguments+=(0 offlineAdmin)
set +e
bash "$release/rk_board.sh" "${arguments[@]}" > "$log" 2>&1
code=$?
set -e
if [[ $code == 0 ]]; then
    case $action in
        start) printf 'SN100已打开，请在RK外接屏幕上操作。\n' ;;
        stop) printf 'SN100已正常关闭，已保存数据保留。\n' ;;
        status)
            if grep -qx 'APP_RUNNING=1' "$log"; then
                printf 'SN100正在运行。\n'
            elif grep -qx 'APP_EXIT=0' "$log"; then
                printf 'SN100已关闭，退出正常。\n'
            else
                tail -8 "$log"
            fi
            ;;
    esac
else
    tail -15 "$log" >&2
    [[ $code != 4 ]] || printf '请在界面完成或放弃未保存操作，再执行stop。\n' >&2
fi
printf '日志：%s\n' "$log"
exit "$code"
