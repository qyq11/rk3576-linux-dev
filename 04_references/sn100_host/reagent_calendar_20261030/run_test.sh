#!/usr/bin/env bash
# 电脑端只上传与收取；全部功能检查在RK执行。
set -Eeuo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
name=$1; target=$2; mode=${3:-offscreen}; scenario=${4:-all}
[[ $name =~ ^[a-z0-9_]+$ && $mode =~ ^(offscreen|eglfs)$ ]]
case "$target" in original_quality_pages_rk_test|original_main_task_chain_rk_test|original_settings_pages_rk_test|original_history_rk_test) ;; *) exit 2;; esac
result=$here/$name; mkdir "$result"
test "$(cat "$here/build_exit_code")" = 0
package=$(cat "$here/packages/latest_package")
root=$(cd -- "$here/../../.." && pwd)
qt_test=$(readlink -f "$root/temp/sn100_host/original_db_20261027/integration/release_work_02/sysroot/usr/lib/aarch64-linux-gnu/libQt5Test.so")
host=lckfb@192.168.100.123; opts=(-o BatchMode=yes -o ConnectTimeout=8)
run=$(ssh "${opts[@]}" "$host" 'umask 077; mktemp -d /tmp/sn100-calendar-20261030.XXXXXX')
[[ $run =~ ^/tmp/sn100-calendar-20261030\.[a-zA-Z0-9]{6}$ ]]
printf '%s\n' "$run" > "$result/remote_path"
scp -B "${opts[@]}" "$package" "$host:$run/package.tar.gz"
scp -B "${opts[@]}" "$here/build/$target" "$host:$run/driver"
scp -B "${opts[@]}" "$qt_test" "$host:$run/libQt5Test.so.5"
scp -B "${opts[@]}" "$here/board_test.sh" "$host:$run/runner.sh"
printf '%s  package.tar.gz\n%s  driver\n%s  libQt5Test.so.5\n%s  runner.sh\n' \
    "$(sha256sum "$package" | cut -d' ' -f1)" "$(sha256sum "$here/build/$target" | cut -d' ' -f1)" \
    "$(sha256sum "$qt_test" | cut -d' ' -f1)" "$(sha256sum "$here/board_test.sh" | cut -d' ' -f1)" > "$result/upload.SHA256SUMS"
if [[ $target == original_history_rk_test ]]; then
    template=$root/06_programs/sn100_host_reference/Seerain450_PC/bin/resource/report_template/report.xml
    cp "$template" "$result/original_report.xml"
    scp -B "${opts[@]}" "$template" "$host:$run/original_report.xml"
    printf '%s  original_report.xml\n' "$(sha256sum "$template" | cut -d' ' -f1)" >> "$result/upload.SHA256SUMS"
fi
scp -B "${opts[@]}" "$result/upload.SHA256SUMS" "$host:$run/UPLOAD_SHA256SUMS"
ssh "${opts[@]}" "$host" bash -s -- "$run" "$mode" "$scenario" <<'REMOTE'
set -Eeuo pipefail
run=$1; mode=$2; scenario=$3
cd "$run"; sha256sum -c UPLOAD_SHA256SUMS
mkdir release; tar --no-same-owner --no-same-permissions -xzf package.tar.gz -C release
test "$(od -An -tu2 -j18 -N2 driver | tr -d ' ')" = 183
chmod 700 driver release/sn100_shell release/sn100_report_renderer
id=$(cat release/release_id); [[ $id =~ ^[a-f0-9]{64}$ ]]
mv release "$id"; printf '%s\n' "$id" > release_name
test -z "$(pgrep -x sn100_shell || true)"
flock -n /home/rkshare/programs/sn100_shell/lifecycle.lock -c true
if [[ $mode == eglfs ]]; then
    owners=$(sudo -n fuser /dev/tty7 /dev/dri/card0 2>&1) && occupied=1 || occupied=0
    [[ $occupied == 0 && -z $owners ]]
    sudo -n systemd-run --quiet --collect --unit=sn100-calendar-20261030 --property=Type=exec \
        /usr/bin/openvt -c 7 -s -w -- /usr/sbin/runuser -u "$(id -un)" -- /bin/bash "$run/runner.sh" "$run" "$mode" "$scenario"
else
    nohup bash "$run/runner.sh" "$run" "$mode" "$scenario" > launch.log 2>&1 < /dev/null &
fi
REMOTE
for attempt in $(seq 1 170); do
    if ssh "${opts[@]}" "$host" "test -f '$run/exit_code'"; then break; fi
    sleep 1
done
for file in app.log exit_code runtime.txt dependencies.txt resources.tsv; do scp -B "${opts[@]}" "$host:$run/$file" "$result/$file"; done
scp -r -B "${opts[@]}" "$host:$run/output" "$result/"
tail -15 "$result/app.log"
printf 'ACTUAL_EXIT_CODE='; cat "$result/exit_code"
test "$(cat "$result/exit_code")" = 0
