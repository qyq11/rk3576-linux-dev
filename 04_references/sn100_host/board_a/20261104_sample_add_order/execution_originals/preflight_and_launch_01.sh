#!/usr/bin/env bash
cd /
base=/home/rkshare/programs/sn100_shell
release=$base/releases/0b0b93787b055f51bba1ac579e0a79488959940284e0a702ab2fa06a1934e743
[[ $(cat /proc/sys/kernel/random/boot_id) == 559493ce-5319-42b6-9383-bf7b93764a35 ]]
[[ $(readlink "$base/current") == releases/ef294a921e7eec2d293beb5aa0fbe5784d6b5056cef92648e799c3529b839a14 ]]
[[ $(readlink /proc/4426/exe) == /userdata/shared/ACA_V1.0OffLineD-500 && $(awk '{print $22}' /proc/4426/stat) == 196590 ]]
[[ $(ss -H -ltnp 'sport = :5000') == *'pid=4426,'* ]]
for entry in /proc/[0-9]*/exe; do
    exe=$(readlink "$entry" 2>/dev/null) || continue
    [[ ${exe##*/} != sn100_shell && ${exe##*/} != sample_add_review ]]
done
for device in /dev/tty7 /dev/dri/card0; do
    code=0;output=$(fuser "$device" 2>&1) || code=$?
    [[ $code == 1 && -z $output ]]
done
sha256sum -c <<'CONFIGURATION'
403348a29b6dafddf4f0511dfd4b8f33277bd56a30b7408e1b5f6e2f54de078e  /home/rkshare/programs/sn100_shell/runtime/data/config.ini
58374e000baaa65c58bb4d3cf1bb84458b4a4de7348e4f54371f2e957fa4b963  /home/rkshare/programs/sn100_shell/runtime/data/config_Debug.ini
029605f35924de646b5402f1e377c93cb26e01952e0daa5fda300fe47f7feed7  /home/rkshare/programs/sn100_shell/runtime/data/theme_config.ini
8b62bdfd565a38dbae844806c1e1ca43f56ad0f93ad5a827118cf21e40cc74fe  /userdata/shared/config_linux.ini
967dfbbde3771201b4cc33b07e9e4f1e37e9359a737bf90bb66441d840822e25  /userdata/shared/consume.ini
9470414d5f95a6b965a5d89e7594fb73ea99fb0743bf2a97bd3bf818d103d86f  /userdata/shared/GL4A_config.cfg
1c6cbb1f065c8eaf0a7701613ea2209320fdd604fb56371e93bf25795aafe116  /userdata/shared/number.ini
CONFIGURATION
runuser -u lckfb -- bash -c 'cd /;source "$1";preflight "$2" "$3"' board-a-preflight "$release/rk_board.sh" "$base" "$release" > "$base/.board_a_preflight_02.log" 2>&1
[[ -z $(grep -E 'not found|Permission denied|ERROR:' "$base/.board_a_preflight_02.log") ]]
printf 'ORDINARY_USER_PREFLIGHT_ACTUAL_EXIT=0\n'
[[ ! -L $base/lifecycle.lock ]]
exec 8>> "$base/lifecycle.lock"
flock -n 8
output=$base/reviews/sample_order_board_a_20261104_01
[[ ! -e $output && ! -L $output ]]
install -d -o lckfb -g lckfb -m 700 "$output"
run=$base/runs/$(date +%Y%m%d_%H%M%S)_$$
[[ ! -e $run ]]
install -d -o lckfb -g lckfb -m 700 "$run"
[[ ! -e $base/current.board_a_20261104_01 ]]
runuser -u lckfb -- ln -s "releases/${release##*/}" "$base/current.board_a_20261104_01"
mv -Tf "$base/current.board_a_20261104_01" "$base/current"
printf '%s\n' "${run##*/}" > "$base/latest_run"
chown lckfb:lckfb "$base/latest_run"
printf 'RUN_DIRECTORY=%s\nRESULT_DIRECTORY=%s\nVARIANT_RELEASE=%s\n' "$run" "$output" "${release##*/}" > "$base/.board_a_launch_20261104_01.txt"
exec 8>&-
systemd-run --quiet --collect --unit=sn100-shell --property=Type=exec --property=Restart=no \
    /usr/bin/openvt -c 7 -s -w -- /usr/sbin/runuser -u lckfb -- /usr/bin/env \
    SN100_SAMPLE_ADD_REVIEW_DIR="$output" /bin/bash "$release/rk_board.sh" supervise "$base" "$release" "$run" 0 ''
printf 'SYSTEMD_LAUNCH_REQUEST_ACTUAL_EXIT=0\n'
cat "$base/.board_a_launch_20261104_01.txt"
for ((i=0;i<60;i++)); do
    if [[ -f $run/app.pid ]]; then
        pid=$(cat "$run/app.pid")
        if [[ -e /proc/$pid/exe ]]; then
            printf 'APP_PID=%s\n' "$pid"
            cat /proc/$pid/stat
            sha256sum /proc/$pid/exe
            printf 'LAUNCHED_UNLIMITED_KEEP_RUNNING=1\n'
            exit 0
        fi
    fi
    if [[ -f $run/exit_code || -f $run/startup_failure.txt ]]; then
        printf 'STARTUP_EXITED_NO_AUTOMATIC_RETRY=1\n';tail -12 "$run/supervisor.log";exit 2
    fi
    sleep .2
done
printf 'STARTUP_STILL_PENDING_NO_SIGNAL_SENT=1\n'
exit 3
