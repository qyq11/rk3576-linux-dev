#!/usr/bin/env bash
# 总控激活：保留09暂存原件，复制到原启动器支持的新正式目录，再切指针。
set -Eeuo pipefail
set -o noclobber
umask 077
[[ $EUID == 0 && $(tr -d '\0\n' </proc/device-tree/serial-number) == 8314b1e8ffd0321f ]]
base=/home/rkshare/programs/sn100_shell
old=releases/08bc43a73a9e8e4826a559f501829355357a46a999799e8b662ebd1feae1a0ef
id=b31f581fd240e09ee4c0a4a4eb6c4cbdd674fa9cf8897fcd170e9d0ca058d993
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
[[ $(sha256sum "$nested/sn100_shell" | cut -d' ' -f1) == 5b108ad35472709fccde35c4cc63d4be64637dcfc7e087839122a548c548d238 ]]
[[ $(sha256sum "$nested/sn100_report_renderer" | cut -d' ' -f1) == 856eed84850e83dcfee04d9d346561a7d132a52ec9670d8d07fa1379de9163d5 ]]
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
[[ $(sha256sum "$before/ini_before.sha256" | cut -d' ' -f1) == 6c961ce4eff4981afa1e7b0e59711fb90eee8b45395e83e2b4049bff6fd58c38 ]]
[[ $(sha256sum "$before/controller_ini_before.sha256" | cut -d' ' -f1) == d82b216b3aee25fb0298565b73a62d7144ede9a1aae1d6840bce6da8ace3b112 ]]
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
