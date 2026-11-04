#!/usr/bin/env bash
[[ $(cat /proc/sys/kernel/random/boot_id) == 559493ce-5319-42b6-9383-bf7b93764a35 ]]
[[ -z $(ss -H -ltnp 'sport = :5000') ]]
for entry in /proc/[0-9]*/exe; do
    exe=$(readlink "$entry" 2>/dev/null) || continue
    [[ ${exe##*/} != ACA_V1.0OffLineD-500 ]]
done
[[ $(sha256sum /userdata/shared/ACA_V1.0OffLineD-500 | cut -d' ' -f1) == ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff ]]
run=/home/rkshare/programs/aca_flow_test/board_a_20261104_01
[[ ! -e $run && ! -L $run ]]
mkdir -p -m 750 "$run"
cat > "$run/supervise.sh" <<'CONTROLLER'
#!/usr/bin/env bash
set -uo pipefail
[[ $(tr -d '\0\n' </proc/device-tree/serial-number) == 3bf8222fe1417f6a ]] || exit 96
run=/home/rkshare/programs/aca_flow_test/board_a_20261104_01
cd /userdata/shared || exit 2
export LD_LIBRARY_PATH=/userdata/shared
stdbuf -oL -eL ./ACA_V1.0OffLineD-500 >"$run/program.log" 2>&1 &
pid=$!
printf '%s\n' "$pid" >"$run/program.pid"
cat /proc/sys/kernel/random/boot_id >"$run/boot_id"
wait "$pid"
code=$?
printf '%s\n' "$code" >"$run/exit_code"
exit "$code"
CONTROLLER
chmod 750 "$run/supervise.sh"
systemd-run --quiet --collect --unit=sn100-original-controller-board-a --property=Type=exec --property=Restart=no --property=WorkingDirectory=/userdata/shared /bin/bash "$run/supervise.sh"
for ((i=0;i<25;i++)); do
    [[ ! -f $run/exit_code ]] || { printf 'CONTROLLER_EXIT=';cat "$run/exit_code";tail -25 "$run/program.log";exit 2; }
    if [[ -n $(ss -H -ltnp 'sport = :5000') ]]; then break;fi
    sleep .2
done
pid=$(cat "$run/program.pid")
[[ $(readlink /proc/$pid/exe) == /userdata/shared/ACA_V1.0OffLineD-500 ]]
sha256sum /proc/$pid/exe
cat /proc/$pid/stat
ss -H -ltnp 'sport = :5000'
[[ -n $(ss -H -ltnp 'sport = :5000') ]]
systemctl show sn100-original-controller-board-a.service -p ActiveState -p SubState -p MainPID -p Restart
printf 'ORIGINAL_CONTROLLER_RUNNING_NO_FINAL_EXIT=1\n'
