#!/usr/bin/env bash
# 将已核验的原文件安装到现有应用的数据位置，保留全部原相对路径。
# 已有同名文件时先停止，便于只对实际受影响的资料决定备份和更新。
set -Eeuo pipefail
[[ $# == 0 && $EUID != 0 ]] || exit 2
host=${RK_HOST:-lckfb@192.168.100.123}
staging=${SN100_ORIGINAL_REMOTE_BASE:-/home/rkshare/programs/sn100_original/20261027_01}
base=${RK_REMOTE_ROOT:-/home/rkshare/programs}/sn100_shell
[[ $host =~ ^[a-zA-Z0-9_.@-]+$ && $staging =~ ^/home/rkshare/programs/sn100_original/[a-zA-Z0-9_-]+$ && $base == /home/rkshare/programs/sn100_shell ]] || exit 2
ssh -o BatchMode=yes -o ConnectTimeout=8 "$host" bash -s -- "$staging" "$base" <<'REMOTE'
set -Eeuo pipefail
umask 077
staging=$1; base=$2; target=$base/runtime/data
test "$(uname -m)" = aarch64
test "$(readlink -f "$base")" = "$base"
test -f "$base/runtime/offline-runtime.tag"
test "$(cat "$base/runtime/offline-runtime.tag")" = sn100-offline-runtime-v1
test ! -L "$base/lifecycle.lock"
exec 9>"$base/lifecycle.lock"; flock -n 9
! systemctl is-active --quiet sn100-shell.service
test -d "$target" && test "$(readlink -f "$target")" = "$target"
(cd "$staging/working" && sha256sum --quiet -c "$staging/original.SHA256SUMS")
conflicts=0; count=0
while IFS= read -r line; do
    relative=${line:66}
    [[ $relative != /* && $relative != *..* && $relative != *\\* ]] || exit 2
    path=$target/$relative
    for parent in "$target" "$(dirname "$path")"; do
        while [[ $parent == "$target"* ]]; do
            test ! -L "$parent"
            [[ $parent != "$target" ]] || break
            parent=$(dirname "$parent")
        done
    done
    if [[ -e $path || -L $path ]]; then
        printf 'EXISTING_FILE_REQUIRES_REVIEW=%s\n' "$relative"
        ((conflicts+=1))
    fi
    ((count+=1))
done < "$staging/original.SHA256SUMS"
test "$count" = 141
test "$conflicts" = 0
# 仅新增原目录下的文件；现有account/hospital/history/catalog等资料不覆盖。
while IFS= read -r line; do
    relative=${line:66}
    mkdir -p "$(dirname "$target/$relative")"
    cp --no-clobber "$staging/working/$relative" "$target/$relative"
    chmod 600 "$target/$relative"
done < "$staging/original.SHA256SUMS"
printf 'sn100-original-work-v1\n' > "$target/.sn100-original-work-v1"
printf '%s\n' "$target" > "$base/original_project_root"
chmod 600 "$target/.sn100-original-work-v1" "$base/original_project_root"
(cd "$target" && sha256sum --quiet -c "$staging/original.SHA256SUMS")
printf 'INSTALLED_ORIGINAL_FILES=%s\nEXISTING_FILES_OVERWRITTEN=0\nORIGINAL_ROOT=%s\n' "$count" "$target"
REMOTE
