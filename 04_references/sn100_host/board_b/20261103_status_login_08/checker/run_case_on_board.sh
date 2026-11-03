#!/usr/bin/env bash
# 仅供总协调在板B运行。auto为真实对象专项；manual为正式程序的人工作业窗口。
# 不部署、不切current、不启停主控，不修改INI，不输入或保存实际账户密码；被动Uh仍可能按原业务写通道库，TX仅Qj不等于无库写入。
set -Eeuo pipefail
umask 077
die() { printf 'STOP: %s\n' "$*" >&2; exit 2; }
[[ $# == 6 ]] || die 'Usage: run_case_on_board.sh {auto|manual} RELEASE_ID APP_SHA256 PROBE_SHA256_OR_DASH MAX_SECONDS ATTEMPT_NAME'
mode=$1; release_id=$2; app_sha=$3; probe_sha=$4; seconds=$5; attempt=$6
[[ $EUID == 0 && $(uname -m) == aarch64 ]] || die '仅由板B root安排，程序仍以lckfb运行。'
[[ $(tr -d '\0\n' </proc/device-tree/serial-number) == 8314b1e8ffd0321f ]] || die '板卡身份不符。'
[[ $mode == auto || $mode == manual ]] || die '未知场景。'
[[ $release_id =~ ^[a-f0-9]{64}$ && $app_sha =~ ^[a-f0-9]{64}$ ]] || die '缺少正式发布编号/应用SHA。'
[[ $release_id != 14ce9714aaa0017c7ad78ff3f551d978672e5def79b96e817605295d7e59020c && $release_id != 7c4abe3062ead1f5b6fc6fd495b708be5f11436504bba5e900b6ccb2c4f5d5f9 && $release_id != e31491a11e2e51764ab49c2c4435ce2f6f1f136223a76323d508ed613a758270 ]] || die '不能使用旧发布代替本轮08。'
[[ $seconds =~ ^[1-9][0-9]{2}$ && $seconds -ge 120 && $seconds -le 600 ]] || die '本轮时长应为120至600秒且不带前导0。'
[[ $attempt =~ ^[a-zA-Z0-9_]{3,64}$ ]] || die '场景名称只用字母数字下划线。'
if [[ $mode == manual ]]; then
    # 在线真实认证可能自动Ur/Up/UP；人工模式本身不构成这些同步已获授权。
    [[ ${SN100_ORIGINAL_LOGIN_SYNC_AUTHORIZED:-0} == 1 ]] || die '尚未登记本次原登录自动同步范围，人工在线认证留待安排。'
    [[ -n ${SN100_AUTHORITY_NOTE:-} && $SN100_AUTHORITY_NOTE != *$'\n'* ]] || die '请记录既有授权出处，不能放入口令。'
fi
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
base=/home/rkshare/programs/sn100_shell
release=$(readlink -f "$base/current")
[[ $release == "$base/releases/$release_id" ]] || die 'current不等于指定新发布；此脚本不切换版本。'
[[ $(sha256sum "$release/sn100_shell" | cut -d' ' -f1) == "$app_sha" ]] || die '正式应用SHA不符。'
source "$release/rk_board.sh"
verify_release "$release"
exec 9>"$base/lifecycle.lock"
flock -n 9 || die '已有程序或生命周期操作占用。'
controller_pid=
for exe in /proc/[0-9]*/exe; do
    path=$(readlink "$exe" || true)
    case "$path" in
        */sn100_shell|*/status_login*review*) die '已有上位机或检查器，不并发运行。' ;;
        /userdata/shared/ACA_V1.0OffLineD-500)
            [[ -z $controller_pid ]] || die '存在多个主控。'
            controller_pid=${exe#/proc/}; controller_pid=${controller_pid%/exe} ;;
    esac
done
[[ -n $controller_pid ]] || die '原主控没有运行；此脚本不会启动第二主控。'
[[ $(sha256sum "/proc/$controller_pid/exe" | cut -d' ' -f1) == ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff ]] || die '主控文件身份不符。'
ss -H -ltnp 'sport = :5000' | grep -q "pid=$controller_pid," || die '5000不是正确主控监听。'
controller_start=$(start_ticks "$controller_pid")
data=$(cat "$base/original_project_root")
configuration=$(cat "$base/instrument_configuration_file")
theme=$(cat "$base/theme_configuration_file")
[[ $data == "$base/runtime/data" && $(readlink -f "$data") == "$data" ]] || die '原资料根发生变化，先核实际配置。'
[[ $configuration == "$data/config.ini" && $theme == "$data/theme_config.ini" ]] || die '实际INI路径与本轮原配置不符。'
[[ -f $data/.sn100-original-work-v1 ]] || die '没有原工作副本标记。'
ini_value() {
    # 只取明确节/键；config.ini可能混有认证字段，不可整份打印或按IP宽搜。
    awk -v wanted_section="$2" -v wanted_key="$3" '
      { sub(/\r$/, ""); line=$0; sub(/^[ \t]+/, "", line); sub(/[ \t]+$/, "", line) }
      line ~ /^[;#]/ { next }
      line ~ /^\[.*\]$/ { section=substr(line,2,length(line)-2); next }
      section==wanted_section && index(line,"=") {
        key=substr(line,1,index(line,"=")-1); sub(/[ \t]+$/, "", key)
        if(key==wanted_key) { value=substr(line,index(line,"=")+1); sub(/^[ \t]+/,"",value); found=1 }
      } END { if(found) print value; else exit 1 }' "$1"
}
[[ $(ini_value "$configuration" tcpmain ip) == 127.0.0.1 ]] || die '主通信地址变化，不猜测或改回。'
[[ $(ini_value "$configuration" tcpmain port) == 5000 ]] || die '主通信端口变化。'
[[ $(ini_value "$data/config_Debug.ini" login open) == true ]] || die '本窄项只适用原login/open=true；不改配置造条件。'
[[ $(ini_value "$data/config_Debug.ini" login defaultPermission) == 0 ]] || die '原默认角色不是0，先重新核对本轮账户范围。'
binary=$release/sn100_shell
if [[ $mode == auto ]]; then
    [[ $probe_sha =~ ^[a-f0-9]{64}$ ]] || die '必须提供本轮08检查器SHA。'
    [[ $(cat "$here/review_release_id") == "$release_id" ]] || die '检查器绑定的正式发布不符。'
    binary=$here/status_login_review_08
    [[ $(sha256sum "$binary" | cut -d' ' -f1) == "$probe_sha" ]] || die '检查器SHA不符。'
fi
run=$here/runs/$attempt
[[ ! -e $run ]] || die '场景结果已存在，不能覆盖。'
install -d -m 700 -o lckfb -g lckfb "$run" "$run/runtime" "$run/results"
if [[ $mode == auto ]]; then chmod 750 "$binary"; chown lckfb:lckfb "$binary"; fi
printf 'MODE=%s\nRELEASE_ID=%s\nAPP_SHA256=%s\nPROBE_SHA256=%s\nCONTROLLER_PID=%s\n' \
    "$mode" "$release_id" "$app_sha" "$probe_sha" "$controller_pid" >"$run/identity.txt"
printf 'CONFIGURATION=%s\nTHEME=%s\nORIGINAL_ROOT=%s\n' "$configuration" "$theme" "$data" >>"$run/identity.txt"
printf 'ORIGINAL_LOGIN_SYNC_AUTHORIZED=%s\n' "${SN100_ORIGINAL_LOGIN_SYNC_AUTHORIZED:-0}" >>"$run/identity.txt"
if [[ $mode == manual ]]; then printf '%s\n' "$SN100_AUTHORITY_NOTE" >"$run/authority_note.txt"; fi
[[ $(cat "$here/shutdown_contract_release_id") == "$release_id" ]] || die '关闭条件不是最终08发布。'
for contract_file in shutdown_contract.json required_shutdown_markers.txt forbidden_shutdown_markers.txt shutdown_contract_release_id; do
    [[ -s $here/$contract_file ]] || die '最终08关闭条件未绑定，不能执行。'
    cp -- "$here/$contract_file" "$run/$contract_file"
done
sha256sum "$run/shutdown_contract.json" "$run/required_shutdown_markers.txt" "$run/forbidden_shutdown_markers.txt" >"$run/shutdown_contract_files.sha256"
date -Is >"$run/started_at.txt"
find "$data" -maxdepth 1 -type f -iname '*.ini' -print0 | sort -z | xargs -0 -r sha256sum >"$run/ini_before.sha256"
sha256sum /userdata/shared/config_linux.ini /userdata/shared/consume.ini \
    /userdata/shared/GL4A_config.cfg /userdata/shared/number.ini >"$run/controller_ini_before.sha256"
runuser -u lckfb -- bash -c 'source "$1/rk_board.sh"; preflight "$2" "$1"' _ "$release" "$base" >"$run/preflight.log" 2>&1
cat >"$run/runtime/kms.json" <<'KMS'
{"device":"/dev/dri/card0","hwcursor":false,"outputs":[{"name":"HDMI1","mode":"preferred","primary":true},{"name":"DP1","mode":"off"},{"name":"Writeback1","mode":"off"}]}
KMS
chown lckfb:lckfb "$run/runtime/kms.json"
printf 'uptime_s\tpid\tutime_ticks\tstime_ticks\trss_kib\tpss_kib\tthreads\tfd_count\n' >"$run/resources.tsv"
cd "$release"
set +e
runuser -u lckfb -- env -u DISPLAY -u WAYLAND_DISPLAY -u LD_PRELOAD \
    XDG_RUNTIME_DIR="$run/runtime" XDG_CACHE_HOME="$run/runtime/cache" \
    QT_QPA_PLATFORM=eglfs QT_QPA_EGLFS_INTEGRATION=eglfs_kms QT_QPA_EGLFS_KMS_CONFIG="$run/runtime/kms.json" \
    QT_PLUGIN_PATH="$release/plugins:/usr/lib/aarch64-linux-gnu/qt5/plugins" \
    QT_QPA_PLATFORM_PLUGIN_PATH="$release/plugins/platforms:/usr/lib/aarch64-linux-gnu/qt5/plugins/platforms" \
    FONTCONFIG_FILE="$release/fonts/fonts.conf" QT_QPA_FONTDIR="$release/fonts" LD_LIBRARY_PATH="$release/lib" \
    QT_LOGGING_RULES='qt.qpa.input=false;qt.qpa.eglfs.kms=true;qt.qpa.egldeviceintegration=true' \
    SN100_CONNECTED_REVIEW_DIR="$run/results" SN100_EXPECTED_RELEASE_ID="$release_id" \
    timeout --signal=TERM --kill-after=15s "$seconds" bash "$here/capture_application_exit.sh" "$binary" "$run/application_exit.txt" \
    --offline --accounts --history --catalog --samples --instrument --password-login --printer-settings --lis-settings \
    --quality-control --calibration --alarm-history --data-dir "$data" --viewport 1600x900 \
    --original-db-root "$data" --start-project-page --instrument-control \
    --instrument-config "$configuration" --theme-config "$theme" >"$run/app.log" 2>&1 &
wrapper=$!
while kill -0 "$wrapper" 2>/dev/null; do
    for exe in /proc/[0-9]*/exe; do
        [[ $(readlink "$exe" 2>/dev/null) == "$binary" ]] || continue
        pid=${exe#/proc/}; pid=${pid%/exe}
        stat=$(cat "/proc/$pid/stat" 2>/dev/null) || continue
        rest=${stat##*) }; read -r -a fields <<<"$rest"
        rss=$(awk '$1=="VmRSS:" {print $2}' "/proc/$pid/status" 2>/dev/null)
        pss=$(awk '$1=="Pss:" {print $2}' "/proc/$pid/smaps_rollup" 2>/dev/null)
        threads=$(awk '$1=="Threads:" {print $2}' "/proc/$pid/status" 2>/dev/null)
        fds=$(find "/proc/$pid/fd" -mindepth 1 -maxdepth 1 -type l 2>/dev/null | wc -l)
        read -r uptime _ </proc/uptime
        printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$uptime" "$pid" "${fields[11]}" "${fields[12]}" \
            "${rss:-unavailable}" "${pss:-unavailable}" "${threads:-unavailable}" "$fds" >>"$run/resources.tsv"
    done
    sleep 1
done
wait "$wrapper"; code=$?
set -e
printf '%s\n' "$code" >"$run/wrapper_exit.txt"
getconf CLK_TCK >"$run/clock_ticks_per_second.txt"
date -Is >"$run/ended_at.txt"
sha256sum -c "$run/ini_before.sha256" >"$run/ini_check.txt" 2>&1 || printf 'INI_CHANGED=1\n' >>"$run/identity.txt"
sha256sum -c "$run/controller_ini_before.sha256" >"$run/controller_ini_check.txt" 2>&1 || printf 'CONTROLLER_INI_CHANGED=1\n' >>"$run/identity.txt"
if [[ $(readlink "/proc/$controller_pid/exe" 2>/dev/null) == /userdata/shared/ACA_V1.0OffLineD-500 \
      && $(start_ticks "$controller_pid" 2>/dev/null) == "$controller_start" ]]; then
    printf 'SAME_CONTROLLER_ALIVE=1\n' >>"$run/identity.txt"
else
    printf 'SAME_CONTROLLER_ALIVE=0\n' >>"$run/identity.txt"
fi
printf 'RUN_DIRECTORY=%s\nWRAPPER_EXIT=%s\n' "$run" "$code"
# 124/137等保留为封装失败，不能用应用日志自报EXIT0替代。
case_code=$code
if [[ $mode == auto && $code == 0 ]]; then
    check_exit=$(cat "$run/results/check_exit.txt" 2>/dev/null || true)
    if [[ $check_exit != 0 ]] || grep -q 'REVIEW_WINDOW_CLOSE_NOT_CONFIRMED' "$run/app.log"; then case_code=1; fi
fi
printf '%s\n' "$case_code" >"$run/case_exit.txt"
exit "$case_code"
