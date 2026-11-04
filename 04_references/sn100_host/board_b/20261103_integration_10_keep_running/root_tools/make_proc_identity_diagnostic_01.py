from pathlib import Path

root = Path(__file__).resolve().parents[3]
calls = root / 'temp/sn100_host/root_manager_20261103'
raw = (calls / 'prepare_flat_batch10_01.sh').read_text()
functions = raw[raw.index('transition_process_exe()'):raw.index('transition_controller_pid()')]
script = '''set -Eeuo pipefail
printf 'UTC='; date -u +%Y-%m-%dT%H:%M:%SZ
''' + functions + '''
failures=0
for node in /proc/[0-9]*/exe; do
    if target=$(transition_process_exe "$node"); then
        case ${target##*/} in ACA_V1.0OffLineD-500*) printf 'CONTROLLER_NODE=%s TARGET=%s\\n' "$node" "$target" ;; esac
    else
        code=$?
        if [[ $code != 1 ]]; then
            printf 'UNCLASSIFIED_NODE=%s STATUS=%s\\n' "$node" "$code"
            dir=${node%/exe}
            printf 'STAT='; cat "$dir/stat" || true
            printf 'COMM='; cat "$dir/comm" || true
            failures=$((failures + 1))
        fi
    fi
done
printf 'UNCLASSIFIED_COUNT=%s\\n' "$failures"
base=/home/rkshare/programs/sn100_shell
printf 'CURRENT='; readlink "$base/current"
printf 'APP_EXE='; readlink /proc/24414/exe
test ! -e "$base/releases/c7cbf438a3a034b25ae22accfaf48bacf0717174ce85bef2203ce8db143cb06e"
printf 'FLAT_NOT_CREATED=1\\nREAD_ONLY_DIAGNOSTIC=1\\n'
'''
with (calls / 'diagnose_proc_identity_batch10_01.sh').open('x', encoding='utf-8', newline='\n') as stream:
    stream.write(script)
