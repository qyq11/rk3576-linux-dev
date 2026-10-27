#!/usr/bin/env bash
# 原数据以SSH流传输，不在电脑生成含账户/患者资料的压缩归档。
set -Eeuo pipefail
[[ $# == 3 && $1 == /* && $2 == /* && $3 == /* && $EUID != 0 ]] || {
    echo 'Usage: deploy.sh ABS_SOURCE_BIN ABS_INVENTORY_JSON ABS_LOCAL_METADATA_DIR' >&2; exit 2;
}
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source=$1; inventory=$2; work=$3
host=${RK_HOST:-lckfb@192.168.100.123}
remote=${SN100_ORIGINAL_REMOTE_BASE:-/home/rkshare/programs/sn100_original/20261027_01}
[[ $host =~ ^[a-zA-Z0-9_.@-]+$ && $remote =~ ^/home/rkshare/programs/sn100_original/[a-zA-Z0-9_-]+$ ]] || exit 2
opts=(-o BatchMode=yes -o ConnectTimeout=8 -o ServerAliveInterval=5 -o ServerAliveCountMax=2)
mkdir -p "$work"
python3 "$here/prepare_manifest.py" --source "$source" --inventory "$inventory" --output "$work"
ssh "${opts[@]}" "$host" bash -s -- "$remote" <<'REMOTE'
set -Eeuo pipefail
umask 077
base=$1
test "$(uname -m)" = aarch64
test ! -e "$base"
mkdir -p "$base/baseline"
test "$(readlink -f "$base")" = "$base"
printf 'NEW_CONTROLLED_PROFILE=%s\n' "$base"
REMOTE
tar -C "$source" --null -T "$work/filelist.nul" -cf - | \
    ssh "${opts[@]}" "$host" "umask 077; tar --no-same-owner --no-same-permissions -xf - -C $remote/baseline"
scp -B "${opts[@]}" "$work/original_manifest.json" "$work/original.SHA256SUMS" "$host:$remote/"
ssh "${opts[@]}" "$host" bash -s -- "$remote" <<'REMOTE'
set -Eeuo pipefail
umask 077
base=$1
(cd "$base/baseline" && sha256sum --quiet -c "$base/original.SHA256SUMS")
cp -a "$base/baseline" "$base/working"
find "$base/baseline" -type f -exec chmod 400 {} +
find "$base/baseline" -type d -exec chmod 500 {} +
find "$base/working" -type d -exec chmod 700 {} +
find "$base/working" -type f -exec chmod 600 {} +
printf 'sn100-original-work-v1\n' > "$base/working/.sn100-original-work-v1"
(cd "$base/working" && sha256sum --quiet -c "$base/original.SHA256SUMS")
printf 'BASELINE_FILES=%s\nWORKING_DATABASES=%s\nINITIAL_SHA_MATCH=1\n' \
    "$(find "$base/baseline" -type f | wc -l)" "$(find "$base/working" -type f -name '*.db' | wc -l)"
stat -c 'ROOT_PERMISSIONS=%a OWNER=%U' "$base" "$base/working"
REMOTE
# 复制后再次检查只读来源，文件变化时不把副本报告为可用。
python3 "$here/prepare_manifest.py" --source "$source" --inventory "$inventory" --output "$work/recheck"
cmp "$work/original_manifest.json" "$work/recheck/original_manifest.json"
printf 'COPY_COMPLETE=1\nREMOTE_PROFILE=%s\n' "$remote"
