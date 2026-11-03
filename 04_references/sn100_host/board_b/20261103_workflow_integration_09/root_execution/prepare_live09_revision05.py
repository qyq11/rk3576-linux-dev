"""实际检查器完成后生成独立上传与单次运行脚本，不在此启动产品。"""
from pathlib import Path
import hashlib
import io
import json
import shlex
import subprocess
import tarfile
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
import board08
here=Path(__file__).resolve().parent
root=here.parents[2]
candidate=root/'temp/sn100_host/board_b/20261103_status_login_09_preparation_04'
assert (here/'checker09_revision05_outer.exit').read_text().strip()=='0'
h=json.loads((candidate/'final_09_handoff.json').read_text(encoding='utf-8'))
release=h['release_id'];app=h['formal_application_sha256'];checker=h['checker_sha256']
build=json.loads((root/'temp/sn100_host/workflow_night_20261102/integration_09_revision_05/build_complete.json').read_text(encoding='utf-8'))
assert h['schema']=='sn100-final-09-checker-handoff-v1' and h['checker_built'] is True
assert release==build['release_id'] and app==build['binaries']['sn100_shell']
assert h['actual_driver_exit_code']==0 and h['before_after_identical'] is True
assert hashlib.sha256((candidate/'status_login_review_09').read_bytes()).hexdigest()==checker
assert all(len(x)==64 and all(c in '0123456789abcdef' for c in x) for x in (release,app,checker))
manifest=candidate/'upload_manifest_09.sha256'
rows=[x.split('  ',1) for x in manifest.read_text().splitlines()]
assert len(rows)==8 and len({n for d,n in rows})==8
assert h['upload_file_count']==8 and {n:d for d,n in rows}==h['upload_files_sha256']
assert hashlib.sha256(manifest.read_bytes()).hexdigest()==h['upload_manifest_sha256']
blob=io.BytesIO()
with tarfile.open(fileobj=blob,mode='w:gz') as out:
    for digest,name in rows:
        assert '/' not in name and hashlib.sha256((candidate/name).read_bytes()).hexdigest()==digest
        out.add(candidate/name,arcname=name,recursive=False)
    out.add(manifest,arcname=manifest.name,recursive=False)
pin=hashlib.sha256(manifest.read_bytes()).hexdigest()
remote='/home/rkshare/programs/sn100_shell/verification/status_login_09_20261103'
script=f'''set -Eeuo pipefail
umask 027
[[ $(tr -d '\\0\\n' </proc/device-tree/serial-number) == 8314b1e8ffd0321f ]]
base=/home/rkshare/programs/sn100_shell
exec 9<>"$base/lifecycle.lock"
flock -n 9
[[ ! -e {remote} && ! -L {remote} ]]
install -d -m 750 -o lckfb -g lckfb {remote}
tar --no-same-owner --no-same-permissions -xzf - -C {remote}
cd {remote}
[[ $(sha256sum upload_manifest_09.sha256 | cut -d' ' -f1) == {pin} ]]
sha256sum -c upload_manifest_09.sha256
chown lckfb:lckfb -- *
chmod 640 -- *
chmod 750 status_login_review_09 run_case_on_board.sh capture_application_exit.sh
runuser -u lckfb -- test -r shutdown_contract.json
runuser -u lckfb -- test -x status_login_review_09
[[ $(cat review_release_id) == {release} ]]
LD_LIBRARY_PATH="$base/releases/integration_09/{release}/lib" ldd status_login_review_09 > checker_runtime_dependencies.txt
if grep -q 'not found' checker_runtime_dependencies.txt;then cat checker_runtime_dependencies.txt;exit 1;fi
printf 'BOARD=board_b\\nSERIAL=8314b1e8ffd0321f\\nCHECKER_UPLOADED=1\\nPROGRAM_STARTED=0\\nMANIFEST_SHA={pin}\\n'
'''
assert not (here/'upload_probe09.log').exists() and not (here/'upload_probe09.exit').exists()
key=subprocess.run(['ssh-keygen','-E','sha256','-l','-f',board08.KNOWN],capture_output=True,check=True)
assert {x.split()[1] for x in key.stdout.decode('ascii').splitlines()}=={board08.PIN}
with (here/'upload_probe09.log').open('xb') as out:
    result=subprocess.run(['ssh',*board08.OPTIONS,'root@192.168.100.123','bash -c '+shlex.quote(script)],input=blob.getvalue(),stdout=out,stderr=subprocess.STDOUT)
(here/'upload_probe09.exit').write_text(str(result.returncode)+'\n')
assert result.returncode==0, '上传失败保留原件，不重复写入'
checks=''
for name in ('ini_before.sha256','controller_ini_before.sha256'):
    p=root/'04_references/sn100_host/board_b/20261103_status_login_08_permission/auto_permission_01'/name
    sha=hashlib.sha256(p.read_bytes()).hexdigest()
    before='/home/rkshare/programs/sn100_shell/verification/status_login_08_permission_20261103/runs/auto_permission_01/'+name
    checks+=f'[[ $(sha256sum {before} | cut -d\' \' -f1) == {sha} ]]\nsha256sum -c {before}\n'
dispatch=f'''#!/usr/bin/env bash
set -Eeuo pipefail
cd {remote}
[[ $(sha256sum upload_manifest_09.sha256 | cut -d' ' -f1) == {pin} ]]
sha256sum --quiet -c upload_manifest_09.sha256
{checks}
# 同一次派发只读采样原主控，用于和本轮上位机短窗并列；不发控制命令。
sample_dir=$PWD/controller_resources_revision05_01
[[ ! -e $sample_dir && ! -L $sample_dir ]]
mkdir -m 750 "$sample_dir"
controller=
for item in /proc/[0-9]*/exe;do
 if [[ $(readlink "$item" || true) == /userdata/shared/ACA_V1.0OffLineD-500 ]];then
  [[ -z $controller ]];controller=${{item#/proc/}};controller=${{controller%/exe}}
 fi
done
[[ -n $controller ]]
[[ $(sha256sum "/proc/$controller/exe" | cut -d' ' -f1) == ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff ]]
sample_boot=$(cat /proc/sys/kernel/random/boot_id)
stat_start=$(cat "/proc/$controller/stat");rest_start=${{stat_start##*) }};read -r -a start_fields <<<"$rest_start"
sample_start=${{start_fields[19]}}
sample_controller() {{
 printf 'uptime_s\\tutime_ticks\\tstime_ticks\\trss_kib\\tpss_kib\\tthreads\\tfd_count\\n'
 for index in $(seq 0 185);do
  [[ ! -e $sample_dir/stop ]] || return 0
  [[ $(cat /proc/sys/kernel/random/boot_id) == "$sample_boot" && $(readlink "/proc/$controller/exe") == /userdata/shared/ACA_V1.0OffLineD-500 ]] || return 1
  stat=$(cat "/proc/$controller/stat");rest=${{stat##*) }};read -r -a fields <<<"$rest"
  [[ ${{fields[19]}} == "$sample_start" ]] || return 1
  read -r uptime _ </proc/uptime
  rss=$(awk '$1=="VmRSS:"{{print $2}}' "/proc/$controller/status")
  pss=$(awk '$1=="Pss:"{{print $2}}' "/proc/$controller/smaps_rollup")
  threads=$(awk '$1=="Threads:"{{print $2}}' "/proc/$controller/status")
  fds=$(find "/proc/$controller/fd" -mindepth 1 -maxdepth 1 -type l | wc -l)
  printf '%s\\t%s\\t%s\\t%s\\t%s\\t%s\\t%s\\n' "$uptime" "${{fields[11]}}" "${{fields[12]}}" "$rss" "$pss" "$threads" "$fds"
  sleep 1
 done
 return 1
}}
sample_controller >"$sample_dir/resources.tsv" & sampler=$!
set +e
bash run_case_on_board.sh auto {release} {app} {checker} 180 auto_revision05_01
actual_case_exit=$?
printf '1\\n' >"$sample_dir/stop"
wait "$sampler";sample_exit=$?
set -e
printf '%s\\n' "$sample_exit" >"$sample_dir/sample.exit"
printf 'CONTROLLER_RESOURCE_BEGIN\\n';cat "$sample_dir/resources.tsv";printf 'CONTROLLER_RESOURCE_END\\n'
printf 'CONTROLLER_RESOURCE_EXIT=%s\\nCONTROLLER_RESOURCE_BOOT=%s\\nCONTROLLER_RESOURCE_PID=%s\\nCONTROLLER_RESOURCE_START_TICKS=%s\\nCONTROLLER_RESOURCE_EXE=/userdata/shared/ACA_V1.0OffLineD-500\\nCONTROLLER_RESOURCE_EXE_SHA256=ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff\\nCONTROLLER_RESOURCE_CLK_TCK=%s\\nACTUAL_CASE_EXIT=%s\\n' "$sample_exit" "$sample_boot" "$controller" "$sample_start" "$(getconf CLK_TCK)" "$actual_case_exit"
exit "$actual_case_exit"
'''
with (here/'run09_revision05.sh').open('x',encoding='utf-8',newline='\n') as out:out.write(dispatch)
with (here/'live09_inputs.json').open('x',encoding='utf-8') as out:json.dump({'release_id':release,'app_sha256':app,'checker_sha256':checker,'upload_manifest_sha256':pin,'remote_directory':remote,'remote_case':remote+'/runs/auto_revision05_01'},out,indent=2)
print('CHECKER_UPLOADED=1; RUN_SCRIPT_PREPARED=1; PRODUCT_STARTED=0')
