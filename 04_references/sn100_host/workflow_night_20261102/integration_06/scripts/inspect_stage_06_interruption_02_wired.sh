#!/usr/bin/env bash
# 只读检查断线后的实际保存位置，不重解包、不移动或删除文件。
set -Eeuo pipefail
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
base=/home/rkshare/programs/sn100_shell
release=14ce9714aaa0017c7ad78ff3f551d978672e5def79b96e817605295d7e59020c
archive=$base/.workflow_upload_fe10b1c27c9f41bea1e265e3b9c5b8fa.tar.gz
date -Is
printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\nCURRENT_RELEASE=%s\n' "$(readlink "$base/current")"
if test -f "$archive"; then stat -c 'UPLOAD_SIZE=%s' "$archive"; sha256sum "$archive"; else printf 'UPLOAD_PRESENT=0\n'; fi
if test -e "$base/releases/$release"; then
  printf 'FINAL_RELEASE_PRESENT=1\n'
  stat -c 'FINAL_MODE=%a OWNER=%U:%G' "$base/releases/$release"
  (cd "$base/releases/$release" && sha256sum --quiet -c SHA256SUMS)
  printf 'FINAL_MANIFEST_OK=1\n'
else printf 'FINAL_RELEASE_PRESENT=0\n'; fi
for path in "$base"/releases/.workflow_stage.*; do
  test -e "$path" || continue
  printf 'STAGE_PARENT=%s\n' "$path"
  find "$path" -mindepth 1 -maxdepth 2 -printf '%y %p\n' | head -80
done
printf 'RELATED_PROCESSES_BEGIN\n'
ps -eo pid,ppid,stat,etimes,args | grep -E '[t]ar --no-same|[s]tage_board_b|[s]ha256sum|[l]dd|[b]ash -s' || true
printf 'RELATED_PROCESSES_END\n'
ss -H -ltnp 'sport = :5000'
printf 'INSPECTION_ONLY=1\n'
