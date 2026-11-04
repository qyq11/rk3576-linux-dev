"""复核结束只读查同一进程、原主控和配置，留驻正式程序。"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
CALLS = ROOT / 'temp/sn100_host/root_manager_20261103'
raw = (CALLS / 'wait_lis_batch10_02.sh').read_text()
prefix = raw.split('root_assert "releases/$release_id"\n', 1)[0]
script = prefix + '''
root_assert "releases/$release_id"
run=$(cat "$meta/launched_run.txt")
[[ $run == "$base/runs/"* && ${run#"$base/runs/"} =~ ^[0-9_]+$ && $(readlink -f "$run") == "$run" ]]
[[ $(cat "$base/latest_run") == "${run##*/}" && ! -e $run/exit_code && ! -e $run/result.txt ]]
pid=$(cat "$run/app.pid")
ticks=$(cat "$run/app.start_ticks")
[[ $pid == 669382 && $ticks == 2102645 && $(transition_start_ticks "$pid") == "$ticks" ]]
[[ $(readlink /proc/$pid/exe) == "$flat/sn100_shell" && $(transition_uid "$pid") == "$(id -u lckfb)" ]]
[[ $(sha256sum /proc/$pid/exe | cut -d' ' -f1) == "$expected_app_sha256" ]]
grep -qx 'SHELL_READY' "$run/app.log"
printf 'BOARD=board_b\\nSERIAL=8314b1e8ffd0321f\\nUTC='; date -u +%Y-%m-%dT%H:%M:%SZ
printf 'CURRENT=%s\\nAPP_PID=%s\\nSTART_TICKS=%s\\nBOOT_ID=%s\\n' "$(readlink "$base/current")" "$pid" "$ticks" "$expected_boot"
printf 'APPLICATION_CMDLINE\\n'; tr '\\0' '\\n' < "/proc/$pid/cmdline"
printf 'APPLICATION_PS\\n'; ps -p "$pid" -o pid=,stat=,lstart=,etimes=,rss=,comm=
printf 'ORIGINAL_CONTROLLER_IDENTITY=%s\\n' "$controller_identity"
systemctl show sn100-shell.service sn100-original-controller-board-b.service opto-camera.service -p Id -p ActiveState -p SubState -p MainPID -p MemoryCurrent -p Nice
ss -lntp '( sport = :5000 or sport = :8080 )'
for node in /proc/[0-9]*/exe; do
    value=$(readlink "$node" 2>/dev/null || true)
    case ${value##*/} in rkaiq_3A_server) printf 'THREE_A_NODE=%s EXE=%s\\n' "$node" "$value" ;; esac
done
[[ -f $run/resources.tsv && ! -L $run/resources.tsv ]]
cp -- "$run/resources.tsv" "$meta/resources_snapshot_01.tsv"
printf 'RESOURCE_SNAPSHOT_ROWS='; wc -l < "$meta/resources_snapshot_01.tsv"
printf 'SHELL_READY=1\\nSAME_NEW_APPLICATION_RUNNING=1\\nAPPLICATION_EXIT=not_evaluated\\nCLOSE_REQUESTED=0\\nSEVEN_CONFIGURATIONS_AND_THREE_POINTERS_UNCHANGED=1\\n'
'''
with (CALLS / 'post_live_batch10_01.sh').open('x', encoding='utf-8', newline='\n') as stream:
    stream.write(script)
