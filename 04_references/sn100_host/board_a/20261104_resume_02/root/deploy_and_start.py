"""只在编译、依赖、打包通过后，将新包另存板A并以原环境启动。"""
from pathlib import Path
import hashlib
import json
import re
import sys
from board_a_rpc import remote, upload

root = Path(__file__).resolve().parents[3]
work = Path(__file__).resolve().parent
for name in ('build', 'dependencies', 'package'):
    assert (work / f'{name}.exit').read_text().strip() == '0', name
package_text = (work / 'packages/latest_package').read_text().strip()
prefix = '/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/'
assert package_text.startswith(prefix)
package = root / package_text[len(prefix):]
match = re.fullmatch(r'sn100_shell_([a-f0-9]{64})\.tar\.gz', package.name)
assert match
release_id = match.group(1)
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
package_sha = sha(package)
binary_sha = sha(work / 'build/sn100_shell')
observer_sha = sha(work / 'live_ui_observer.so')
supervisor_sha = sha(work / 'live_supervisor.sh')
base = '/home/rkshare/programs/sn100_shell'
verification = base + '/verification/resume_20261104_02'
release = base + '/releases/' + release_id
incoming = base + '/incoming/resume_20261104_02_' + release_id + '.tar.gz'
original_current = base + '/releases/0b0b93787b055f51bba1ac579e0a79488959940284e0a702ab2fa06a1934e743'
initial = (work / 'identity_before.stdout').read_text()
config_rows = initial.split('CONFIG_HASHES\n', 1)[1].split('CONFIG_SAFE_VALUES', 1)[0].strip().splitlines()
assert len(config_rows) == 7
checks = []
for row in config_rows:
    digest, path = row.split('  ', 1)
    assert re.fullmatch('[a-f0-9]{64}', digest) and re.fullmatch(r'/[A-Za-z0-9_./-]+', path)
    checks.append(f'[[ $(sha256sum "{path}" | cut -d" " -f1) == {digest} ]]')
configs = '\n'.join(checks)
preflight = rf'''cd /
base={base}
[[ $(readlink -f "$base/current") == {original_current} ]]
{configs}
controller=$(ss -H -ltnp 'sport = :5000' | sed -n 's/.*pid=\([0-9]*\),.*/\1/p')
[[ $controller =~ ^[0-9]+$ ]]
[[ $(readlink /proc/$controller/exe) == /userdata/shared/ACA_V1.0OffLineD-500 ]]
printf 'BOOT=%s\nCONTROLLER_PID=%s\nCONTROLLER_TICKS=%s\n' "$(cat /proc/sys/kernel/random/boot_id)" "$controller" "$(awk '{{print $22}}' /proc/$controller/stat)"
for entry in /proc/[0-9]*/exe; do
 exe=$(readlink "$entry" 2>/dev/null) || continue
 [[ ${{exe##*/}} != sn100_shell && ${{exe##*/}} != sample_add_review ]]
done
install -d -m 700 "$base/incoming"
[[ ! -e {verification} && ! -L {verification} ]]
install -d -o lckfb -g lckfb -m 700 {verification}
'''
result = remote('deployment_preflight', preflight)
assert result.returncode == 0, result.stderr.decode(errors='replace')
state = dict(line.split('=', 1) for line in result.stdout.decode().splitlines() if '=' in line)
assert re.fullmatch('[a-f0-9-]{36}', state['BOOT']) and state['CONTROLLER_PID'].isdigit() and state['CONTROLLER_TICKS'].isdigit()
(work / 'deployment_state.json').write_text(json.dumps(state, indent=2) + '\n')
upload(package, incoming)
upload(work / 'live_ui_observer.so', verification + '/live_ui_observer.so')
upload(work / 'live_supervisor.sh', verification + '/live_supervisor.sh')
launch = f'''cd /
base={base}
release={release}
verification={verification}
[[ $(cat /proc/sys/kernel/random/boot_id) == {state['BOOT']} ]]
[[ $(readlink /proc/{state['CONTROLLER_PID']}/exe) == /userdata/shared/ACA_V1.0OffLineD-500 ]]
[[ $(awk '{{print $22}}' /proc/{state['CONTROLLER_PID']}/stat) == {state['CONTROLLER_TICKS']} ]]
[[ $(ss -H -ltnp 'sport = :5000') == *'pid={state['CONTROLLER_PID']},'* ]]
[[ $(readlink -f "$base/current") == {original_current} ]]
{configs}
[[ $(sha256sum {incoming} | cut -d' ' -f1) == {package_sha} ]]
[[ $(sha256sum "$verification/live_ui_observer.so" | cut -d' ' -f1) == {observer_sha} ]]
[[ $(sha256sum "$verification/live_supervisor.sh" | cut -d' ' -f1) == {supervisor_sha} ]]
[[ ! -L "$base/lifecycle.lock" ]]
exec 8>> "$base/lifecycle.lock"
flock -n 8
for entry in /proc/[0-9]*/exe; do
 exe=$(readlink "$entry" 2>/dev/null) || continue
 [[ ${{exe##*/}} != sn100_shell && ${{exe##*/}} != sample_add_review ]]
done
[[ ! -e $release && ! -L $release && ! -L "$base/releases" ]]
stage=$(mktemp -d "$base/releases/.resume_20261104_02.XXXXXXXX")
install -d -m 700 "$stage/{release_id}"
tar --no-same-owner --no-same-permissions -xzf {incoming} -C "$stage/{release_id}"
source "$stage/{release_id}/rk_board.sh"
verify_release "$stage/{release_id}"
mv -T "$stage/{release_id}" "$release"
rmdir "$stage"
chown -R lckfb:lckfb "$release"
chown lckfb:lckfb "$verification/live_ui_observer.so" "$verification/live_supervisor.sh"
chmod 600 "$verification/live_ui_observer.so"
chmod 700 "$verification/live_supervisor.sh"
for object in "$release/sn100_shell" "$release/sn100_report_renderer" "$verification/live_ui_observer.so"; do
 LD_LIBRARY_PATH="$release/lib" ldd "$object"
done > "$verification/dependencies.txt"
! grep -q 'not found' "$verification/dependencies.txt"
runuser -u lckfb -- bash -c 'source "$1"; preflight "$2" "$3"' board-a-preflight "$release/rk_board.sh" "$base" "$release"
run=$base/runs/$(date +%Y%m%d_%H%M%S)_resume02_$$
[[ ! -e $run && ! -L $run ]]
install -d -o lckfb -g lckfb -m 700 "$run"
printf 'RUN_DIRECTORY=%s\nRELEASE_ID=%s\nCURRENT_UNCHANGED=1\n' "$run" '{release_id}'
exec 8>&-
systemd-run --quiet --collect --unit=sn100-resume-20261104-02 --property=Type=exec --property=Restart=no \
 /usr/bin/openvt -c 7 -s -w -- /usr/sbin/runuser -u lckfb -- /bin/bash "$verification/live_supervisor.sh" "$base" "$release" "$run" "$verification"
for ((i=0;i<75;i++)); do
 if [[ -f $run/app.pid && -e /proc/$(cat "$run/app.pid")/exe ]]; then
  [[ $(sha256sum /proc/$(cat "$run/app.pid")/exe | cut -d' ' -f1) == {binary_sha} ]]
  printf 'APP_PID=%s\nAPP_SHA256=%s\n' "$(cat "$run/app.pid")" '{binary_sha}'
  exit 0
 fi
 if [[ -f $run/exit_code || -f $run/startup_failure.txt ]]; then
  tail -20 "$run/supervisor.log"
  exit 2
 fi
 sleep .2
done
exit 3
'''
(work / 'board_a_launch.sh').write_text(launch)
result = remote('board_a_launch', launch, 120)
print(result.stdout.decode(errors='replace'))
print(result.stderr.decode(errors='replace'), file=sys.stderr)
metadata = {'release_id': release_id, 'package_sha256': package_sha, 'binary_sha256': binary_sha,
            'observer_sha256': observer_sha, 'supervisor_sha256': supervisor_sha,
            'package': str(package), 'launch_exit': result.returncode, 'controller': state,
            'config_count': len(config_rows), 'default_current_changed': False}
if result.returncode == 0:
    output = result.stdout.decode()
    metadata['run'] = re.search(r'^RUN_DIRECTORY=(.+)$', output, re.MULTILINE).group(1)
    metadata['pid'] = int(re.search(r'^APP_PID=(\d+)$', output, re.MULTILINE).group(1))
(work / 'deployment_result.json').write_text(json.dumps(metadata, indent=2) + '\n')
sys.exit(result.returncode)
