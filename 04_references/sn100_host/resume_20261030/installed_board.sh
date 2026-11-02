#!/usr/bin/env bash
# 只使用本轮测试自产匿名资料，验证正式安装的程序两次实屏启动和正常退出。
set -Eeuo pipefail
run=$1; fixture=$2; expected=$3
[[ $run =~ ^/tmp/sn100-installed-20261030\.[a-zA-Z0-9]{6}$ && $(readlink -f "$run") == "$run" && $EUID != 0 ]]
[[ $fixture =~ ^/tmp/sn100-resume-20261030\.[a-zA-Z0-9]{6}/data/quality_pages_[a-zA-Z0-9]+$ ]]
test "$(cat "$fixture/.sn100-original-work-v1")" = sn100-original-work-v1
grep -qx '# test settings' "$fixture/config.ini"
exec 9>/home/rkshare/programs/sn100_shell/lifecycle.lock
flock -n 9
release=$(readlink -f /home/rkshare/programs/sn100_shell/current)
source "$release/rk_board.sh"; verify_release "$release"
test "$(sha256sum "$release/sn100_shell" | cut -d' ' -f1)" = "$expected"
runtime_environment "$release" "$run/cache"
ldd "$release/sn100_shell" > "$run/dependencies.txt"
! grep -q 'not found' "$run/dependencies.txt"
test "$(od -An -tu2 -j18 -N2 "$release/sn100_shell" | tr -d ' ')" = 183
mkdir "$run/data" "$run/xdg"; chmod 700 "$run/xdg"
cp -a "$fixture/." "$run/data/"
export XDG_RUNTIME_DIR=$run/xdg
cat > "$run/kms.json" <<'KMS'
{"device":"/dev/dri/card0","hwcursor":false,"outputs":[{"name":"HDMI1","mode":"preferred","primary":true},{"name":"DP1","mode":"off"},{"name":"Writeback1","mode":"off"}]}
KMS
export QT_QPA_EGLFS_KMS_CONFIG=$run/kms.json
{ date -Is; uname -a; printf 'RELEASE=%s\nANONYMOUS_ONLY=1\nREAL_INSTRUMENT=0\nCWD=%s\n' "$release" "$release"; sha256sum "$release/sn100_shell"; } > "$run/runtime.txt"
cd "$release"
for cycle in 1 2; do
    "$release/sn100_shell" --offline --accounts --catalog --quality-control --calibration --password-login \
        --original-db-root "$run/data" --data-dir "$run/data" --viewport 1600x900 > "$run/app_$cycle.log" 2>&1 &
    child=$!
    ready=0
    for attempt in $(seq 1 100); do
        if grep -qx SHELL_READY "$run/app_$cycle.log"; then ready=1; break; fi
        kill -0 "$child" 2>/dev/null || break
        sleep .2
    done
    if [[ $ready == 1 ]]; then sleep 10; fi
    kill -TERM "$child" 2>/dev/null || true
    for attempt in $(seq 1 50); do
        kill -0 "$child" 2>/dev/null || break
        sleep .2
    done
    if kill -0 "$child" 2>/dev/null; then kill -KILL "$child"; fi
    set +e; wait "$child"; code=$?; set -e
    printf '%s\n' "$code" > "$run/exit_$cycle"
    [[ $code == 0 && $ready == 1 ]]
    grep -q 'THREADS_STOPPED true' "$run/app_$cycle.log"
    grep -q 'QUALITY_CONTROL_READY' "$run/app_$cycle.log"
    grep -q 'CALIBRATION_READY' "$run/app_$cycle.log"
done
printf '0\n' > "$run/exit_code"
