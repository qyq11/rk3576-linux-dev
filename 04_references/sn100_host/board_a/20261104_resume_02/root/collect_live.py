"""只回收本轮新进程的白名单日志快照，不接触业务数据库。"""
from pathlib import Path
import hashlib
import io
import json
import re
import sys
import tarfile
from board_a_rpc import remote

work = Path(__file__).resolve().parent
tag = sys.argv[1] if len(sys.argv) > 1 else ''
assert re.fullmatch('[a-z_]*', tag)
suffix = '_' + tag if tag else ''
meta = json.loads((work / 'deployment_result_v2.json').read_text())
run = meta['run']
assert re.fullmatch(r'/home/rkshare/programs/sn100_shell/runs/[A-Za-z0-9_]+', run)
release = '/home/rkshare/programs/sn100_shell/releases/' + meta['release_id']
script = f'''cd /
run={run}
base=/home/rkshare/programs/sn100_shell
verification=$base/verification/resume_20261104_02
[[ $(cat "$run/release_id") == {meta['release_id']} ]]
if [[ -f $run/app.pid && -e /proc/$(cat "$run/app.pid")/exe ]]; then
 [[ $(readlink /proc/$(cat "$run/app.pid")/exe) == {release}/sn100_shell ]]
 [[ $(sha256sum /proc/$(cat "$run/app.pid")/exe | cut -d' ' -f1) == {meta['binary_sha256']} ]]
 [[ $(awk '{{print $22}}' /proc/$(cat "$run/app.pid")/stat) == $(cat "$run/app.start_ticks") ]]
fi
snapshot=$(mktemp -d "$verification/collection.XXXXXXXX")
for name in app.log supervisor.log startup_failure.txt runtime.txt resources.tsv release_id app.pid app.start_ticks app.exe boot_id exit_code result.txt kms.json observer_output; do
 if [[ -f $run/$name && ! -L $run/$name ]]; then cp --reflink=auto "$run/$name" "$snapshot/$name"; fi
done
observer=$verification/ui_$(basename "$run").jsonl
if [[ -f $observer && ! -L $observer ]]; then cp --reflink=auto "$observer" "$snapshot/live_ui_observer.jsonl"; fi
{{
 printf 'CURRENT='; readlink -f "$base/current"
 printf 'CONTROLLER='; readlink /proc/{meta['controller']['CONTROLLER_PID']}/exe
 printf 'CONTROLLER_TICKS='; awk '{{print $22}}' /proc/{meta['controller']['CONTROLLER_PID']}/stat
 printf 'CLOCK_TICKS_PER_SECOND='; getconf CLK_TCK
 ss -H -ltnp 'sport = :5000'
 find "$base/runtime/data" -maxdepth 2 -type f \\( -iname '*.ini' -o -iname '*.cfg' \\) -print0 | sort -z | xargs -0 -r sha256sum
}} > "$snapshot/current_state.txt"
(cd "$snapshot" && sha256sum -- * > SHA256SUMS)
tar -czf - -C "$snapshot" .
'''
result = remote('live_collection' + suffix, script)
assert result.returncode == 0, result.stderr.decode(errors='replace')
out = work / ('collected' + suffix)
out.mkdir(exist_ok=False)
with tarfile.open(fileobj=io.BytesIO(result.stdout), mode='r:gz') as tar:
    for entry in tar:
        if entry.name == '.':
            assert entry.isdir()
            continue
        name = entry.name.removeprefix('./')
        assert entry.isfile() and '/' not in name and entry.size < 30_000_000
        (out / name).write_bytes(tar.extractfile(entry).read())
for line in (out / 'SHA256SUMS').read_text().splitlines():
    digest, name = line.split('  ', 1)
    assert hashlib.sha256((out / name).read_bytes()).hexdigest() == digest
summary = {'files': len(list(out.iterdir())), 'sha_mismatches': 0, 'run': run,
           'result_present': (out / 'result.txt').exists(), 'observer_present': (out / 'live_ui_observer.jsonl').exists()}
(work / ('collection_result' + suffix + '.json')).write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps(summary))
if summary['observer_present']:
    for line in (out / 'live_ui_observer.jsonl').read_text().splitlines()[:5]:
        print(line)
