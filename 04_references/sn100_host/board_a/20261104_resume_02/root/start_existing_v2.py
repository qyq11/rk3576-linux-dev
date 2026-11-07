from pathlib import Path
import hashlib
import json
import re
import sys
from board_a_rpc import remote, upload

work = Path(__file__).resolve().parent
meta = json.loads((work / 'deployment_result.json').read_text())
assert meta['launch_exit'] == 3
base = '/home/rkshare/programs/sn100_shell'
release = base + '/releases/' + meta['release_id']
verification = base + '/verification/resume_20261104_02'
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
wrapper_sha = sha(work / 'live_supervisor_v2.sh')
upload(work / 'live_supervisor_v2.sh', verification + '/live_supervisor_v2.sh')
initial = (work / 'identity_before.stdout').read_text()
rows = initial.split('CONFIG_HASHES\n', 1)[1].split('CONFIG_SAFE_VALUES', 1)[0].strip().splitlines()
checks = []
for row in rows:
    digest, path = row.split('  ', 1)
    assert re.fullmatch('[a-f0-9]{64}', digest) and re.fullmatch(r'/[A-Za-z0-9_./-]+', path)
    checks.append(f'[[ $(sha256sum "{path}" | cut -d" " -f1) == {digest} ]]')
assert len(checks) == 7
configs = '\n'.join(checks)
state = meta['controller']
script = f'''cd /
base={base}; release={release}; verification={verification}
[[ $(cat /proc/sys/kernel/random/boot_id) == {state['BOOT']} ]]
[[ $(readlink /proc/{state['CONTROLLER_PID']}/exe) == /userdata/shared/ACA_V1.0OffLineD-500 ]]
[[ $(awk '{{print $22}}' /proc/{state['CONTROLLER_PID']}/stat) == {state['CONTROLLER_TICKS']} ]]
[[ $(ss -H -ltnp 'sport = :5000') == *'pid={state['CONTROLLER_PID']},'* ]]
[[ $(readlink -f "$base/current") == "$base/releases/0b0b93787b055f51bba1ac579e0a79488959940284e0a702ab2fa06a1934e743" ]]
{configs}
[[ $(sha256sum "$verification/live_ui_observer.so" | cut -d' ' -f1) == {meta['observer_sha256']} ]]
[[ $(sha256sum "$verification/live_supervisor_v2.sh" | cut -d' ' -f1) == {wrapper_sha} ]]
chown lckfb:lckfb "$verification/live_supervisor_v2.sh"
chmod 700 "$verification/live_supervisor_v2.sh"
[[ ! -L "$base/lifecycle.lock" ]]
exec 8>> "$base/lifecycle.lock"; flock -n 8
for entry in /proc/[0-9]*/exe; do
 exe=$(readlink "$entry" 2>/dev/null) || continue
 [[ ${{exe##*/}} != sn100_shell && ${{exe##*/}} != sample_add_review ]]
done
source "$release/rk_board.sh"; verify_release "$release"
run=$base/runs/$(date +%Y%m%d_%H%M%S)_$$
[[ ! -e $run && ! -L $run ]]
install -d -o lckfb -g lckfb -m 700 "$run"
printf 'RUN_DIRECTORY=%s\n' "$run"
exec 8>&-
systemd-run --quiet --collect --unit=sn100-resume-20261104-02-b --property=Type=exec --property=Restart=no \
 /usr/bin/openvt -c 7 -s -w -- /usr/sbin/runuser -u lckfb -- /bin/bash "$verification/live_supervisor_v2.sh" "$base" "$release" "$run" "$verification"
for ((i=0;i<100;i++)); do
 if [[ -f $run/app.pid && -e /proc/$(cat "$run/app.pid")/exe ]]; then
  [[ $(sha256sum /proc/$(cat "$run/app.pid")/exe | cut -d' ' -f1) == {meta['binary_sha256']} ]]
  printf 'APP_PID=%s\n' "$(cat "$run/app.pid")"
  exit 0
 fi
 if [[ -f $run/exit_code || -f $run/startup_failure.txt ]]; then
  tail -20 "$run/supervisor.log"; exit 2
 fi
 sleep .2
done
exit 3
'''
(work / 'board_a_start_v2.sh').write_text(script)
result = remote('board_a_start_v2', script)
print(result.stdout.decode(errors='replace')); print(result.stderr.decode(errors='replace'), file=sys.stderr)
meta['first_launch_exit'] = meta['launch_exit']
meta['launch_exit'] = result.returncode
meta['supervisor_v2_sha256'] = wrapper_sha
if result.returncode == 0:
    text = result.stdout.decode()
    meta['run'] = re.search(r'^RUN_DIRECTORY=(.+)$', text, re.MULTILINE).group(1)
    meta['pid'] = int(re.search(r'^APP_PID=(\d+)$', text, re.MULTILINE).group(1))
(work / 'deployment_result_v2.json').write_text(json.dumps(meta, indent=2) + '\n')
sys.exit(result.returncode)
