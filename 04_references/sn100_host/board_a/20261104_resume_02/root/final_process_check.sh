base=/home/rkshare/programs/sn100_shell
run=$base/runs/20261104_193341_108489
pid=$(cat "$run/app.pid")
[[ $pid == 109249 && -e /proc/$pid/exe ]]
[[ $(readlink /proc/$pid/exe) == "$base/releases/87768ca46fef52bc6323ff6a24eb920ff7bb1c0344279e12881b0aa44e2ea3b8/sn100_shell" ]]
[[ $(awk '{print $22}' /proc/$pid/stat) == $(cat "$run/app.start_ticks") ]]
[[ $(sha256sum /proc/$pid/exe | cut -d' ' -f1) == 4bffa0b3c4d0b6928b1555ee62550cdddbdfe0a000babd79d0542be4b6548328 ]]
printf 'CHECK_TIME='; date --iso-8601=seconds
printf 'APP_PID=%s\nAPP_START_TICKS=' "$pid"; cat "$run/app.start_ticks"
ss -H -tnp | awk -v marker="pid=$pid," 'index($0,marker) {print}'
printf 'APP_ALIVE=1\nEXIT_RESULT_PRESENT='; if [[ -f $run/result.txt ]]; then printf '1\n'; else printf '0\n'; fi
printf 'CURRENT='; readlink -f "$base/current"
printf 'CONTROLLER='; readlink /proc/4426/exe
printf 'CONTROLLER_START_TICKS='; awk '{print $22}' /proc/4426/stat
