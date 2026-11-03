"""完成全部保存归档后，生成只复制新版本和切换指针的板B脚本。"""
from pathlib import Path
import hashlib
import json
import re

here=Path(__file__).resolve().parent
work=here.parents[2]/'temp/sn100_host/workflow_night_20261102/integration_09_revision_05'
assert all((here/(name+'09_revision05_outer.exit')).read_text().strip()=='0' for name in ['build','stage','archive','collect','finish'])
build=json.loads((work/'build_complete.json').read_text(encoding='utf-8'))
finish=json.loads((work/'finish_complete.json').read_text(encoding='utf-8'))
assert finish['complete'] and finish['release_id']==build['release_id']
values={'__NEW_RELEASE__':build['release_id'],'__APP_SHA__':build['binaries']['sn100_shell'],
        '__RENDER_SHA__':build['binaries']['sn100_report_renderer']}
saved=here.parents[2]/'04_references/sn100_host/board_b/20261103_status_login_08_permission/auto_permission_01'
values['__INI_LIST_SHA__']=hashlib.sha256((saved/'ini_before.sha256').read_bytes()).hexdigest()
values['__CTRL_LIST_SHA__']=hashlib.sha256((saved/'controller_ini_before.sha256').read_bytes()).hexdigest()
assert all(re.fullmatch('[a-f0-9]{64}',v) for v in values.values())
script=r'''#!/usr/bin/env bash
# 总控激活：保留09暂存原件，复制到原启动器支持的新正式目录，再切指针。
set -Eeuo pipefail
set -o noclobber
umask 077
[[ $EUID == 0 && $(tr -d '\0\n' </proc/device-tree/serial-number) == 8314b1e8ffd0321f ]]
base=/home/rkshare/programs/sn100_shell
old=releases/08bc43a73a9e8e4826a559f501829355357a46a999799e8b662ebd1feae1a0ef
id=__NEW_RELEASE__
nested=$base/releases/integration_09/$id
new=$base/releases/$id
audit=$base/verification/integration_09_activation_20261103
[[ $(readlink -f "$base") == "$base" && $(readlink -f "$base/releases") == "$base/releases" ]]
[[ -f $base/lifecycle.lock && ! -L $base/lifecycle.lock ]]
exec 9<>"$base/lifecycle.lock"
flock -n 9
[[ -L $base/current && $(readlink "$base/current") == "$old" ]]
[[ $(readlink -f "$nested") == "$nested" && ! -L $nested ]]
[[ ! -e $new && ! -L $new && ! -e $audit && ! -L $audit ]]
source "$nested/rk_board.sh"
verify_release "$nested"
[[ $(sha256sum "$nested/sn100_shell" | cut -d' ' -f1) == __APP_SHA__ ]]
[[ $(sha256sum "$nested/sn100_report_renderer" | cut -d' ' -f1) == __RENDER_SHA__ ]]
controller_pid=
for exe in /proc/[0-9]*/exe; do
 path=$(readlink "$exe" || true)
 case "$path" in
  */sn100_shell|*/sn100_shell" (deleted)"|*/sn100_report_renderer|*/sn100_report_renderer" (deleted)"|*/status_login*review*) printf 'APP_ALREADY_RUNNING=%s\n' "$exe"; exit 3 ;;
  /userdata/shared/ACA_V1.0OffLineD-500)
   [[ -z $controller_pid ]]; controller_pid=${exe#/proc/};controller_pid=${controller_pid%/exe} ;;
 esac
done
[[ -n $controller_pid && $(sha256sum "/proc/$controller_pid/exe" | cut -d' ' -f1) == ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff ]]
ss -H -ltnp 'sport = :5000' | grep -q "pid=$controller_pid,"
before=$base/verification/status_login_08_permission_20261103/runs/auto_permission_01
[[ $(sha256sum "$before/ini_before.sha256" | cut -d' ' -f1) == __INI_LIST_SHA__ ]]
[[ $(sha256sum "$before/controller_ini_before.sha256" | cut -d' ' -f1) == __CTRL_LIST_SHA__ ]]
sha256sum -c "$before/ini_before.sha256"
sha256sum -c "$before/controller_ini_before.sha256"
install -d -m 750 "$audit"
printf '%s\n' "$old" >"$audit/current_before.txt"
date -Is >"$audit/started_at.txt"
printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\nCONTROLLER_PID=%s\n' "$controller_pid" >"$audit/identity.txt"
cp -a -- "$nested" "$new"
verify_release "$new"
verify_release "$nested"
[[ $(cat "$new/release_id") == "$id" && $(stat -c '%u:%g' "$base/current") == 0:0 ]]
[[ $(readlink "$base/current") == "$old" ]]
temporary=$base/.current-09-$$
[[ ! -e $temporary && ! -L $temporary ]]
ln -s "releases/$id" "$temporary"
chown -h 0:0 "$temporary"
[[ -L $base/current && $(readlink "$base/current") == "$old" ]]
mv -Tf -- "$temporary" "$base/current"
[[ $(current_release "$base") == "$new" ]]
printf '%s\n' "$(readlink "$base/current")" >"$audit/current_after.txt"
date -Is >"$audit/ended_at.txt"
printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\nCURRENT_BEFORE=%s\nCURRENT_AFTER=%s\nNESTED_PRESERVED=1\nACTIVATED=1\nPROGRAM_STARTED=0\nCONTROLLER_RESTARTED=0\nAUDIT_DIRECTORY=%s\n' "$old" "$(readlink "$base/current")" "$audit"
'''
for key,value in values.items():script=script.replace(key,value)
with (here/'activate09.sh').open('x',encoding='utf-8',newline='\n') as out:out.write(script)
print('ACTIVATION_SCRIPT_PREPARED=1; BOARD_NOT_ACCESSED=1')
