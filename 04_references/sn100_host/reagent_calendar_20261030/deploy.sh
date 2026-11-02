#!/usr/bin/env bash
set -Eeuo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
module_dir=$root/06_programs/sn100_host/shell
work=$here
host=lckfb@192.168.100.123
base=/home/rkshare/programs/sn100_shell
opts=(-o BatchMode=yes -o ConnectTimeout=6 -o ServerAliveInterval=5 -o ServerAliveCountMax=2)
staged_upload=
test "$(cat "$work/build_exit_code")" = 0
python3 "$module_dir/package_arm64.py" snapshot --output "$work/pre_deploy_snapshot.json"
cmp "$work/source_snapshot.json" "$work/pre_deploy_snapshot.json"
test -s "$work/packages/latest_package"
remote() {
    local command
    printf -v command '%q ' bash -c "$1" -- "${@:2}"
    ssh -T "${opts[@]}" "$host" "$command"
}
deploy() {
    local package checksum release_id
    package=$(cat "$work/packages/latest_package")
    checksum=$(sha256sum "$package" | cut -d' ' -f1)
    release_id=${package##*/sn100_shell_}; release_id=${release_id%.tar.gz}
    [[ $release_id =~ ^[a-f0-9]{64}$ ]] || { echo 'Unexpected release package.' >&2; exit 1; }
    staged_upload=$(remote 'set -euo pipefail; test "$(uname -m)" = aarch64; test "$(id -u)" != 0; test ! -L "$1"; mkdir -p "$1"; test "$(readlink -f "$1")" = "$1"; mktemp "$1/.upload.XXXXXXXX"' "$base")
    [[ $staged_upload == "$base"/.upload.* && $staged_upload != *$'\n'* ]] || { echo 'Invalid upload staging path.' >&2; exit 1; }
    cleanup_upload() {
        local result=$?
        trap - EXIT
        if [[ -n $staged_upload ]]; then remote 'rm -f -- "$1"' "$staged_upload" || true; fi
        return "$result"
    }
    trap cleanup_upload EXIT
    scp -B "${opts[@]}" "$package" "$host:$staged_upload"
    remote '
set -Eeuo pipefail
base=$1; upload=$2; expected=$3; release_id=$4
test ! -L "$base/lifecycle.lock"
exec 9>"$base/lifecycle.lock"
flock -n 9 || { echo "Running deployment is locked; request normal stop first." >&2; exit 1; }
if systemctl is-active --quiet sn100-shell.service; then echo "Graphical service is active." >&2; exit 1; fi
test "$(sha256sum "$upload" | cut -d" " -f1)" = "$expected"
test ! -L "$base/releases"
mkdir -p "$base/releases"
stage_parent=$(mktemp -d "$base/releases/.stage.XXXXXXXX")
stage=$stage_parent/$release_id
mkdir "$stage"
trap '\''rm -rf -- "$stage_parent"'\'' EXIT
tar --no-same-owner --no-same-permissions -xzf "$upload" -C "$stage"
test "$(cat "$stage/release_id")" = "$release_id"
test "$(sha256sum "$stage/SHA256SUMS" | cut -d" " -f1)" = "$release_id"
(cd "$stage" && sha256sum -c SHA256SUMS)
chmod 700 "$stage/sn100_shell"
if [[ -f $stage/sn100_report_renderer ]]; then chmod 700 "$stage/sn100_report_renderer"; fi
# Verify with the helper from the newly checked payload, including the complete
# file set. Do not trust a self-consistent but wrong existing release directory.
source "$stage/rk_board.sh"
verify_release "$stage"
test ! -L "$base/deploy_checks"
mkdir -p "$base/deploy_checks"
dependencies=$(mktemp "$base/deploy_checks/.check.XXXXXXXX")
objects=("$stage/sn100_shell" "$stage/plugins/sqldrivers/libqsqlite.so")
if [[ -f $stage/sn100_report_renderer ]]; then objects+=("$stage/sn100_report_renderer" "$stage/plugins/platforms/libqoffscreen.so"); fi
for object in "${objects[@]}"; do
    LD_LIBRARY_PATH="$stage/lib" ldd "$object" >> "$dependencies"
done
cat "$dependencies"
! grep -q "not found" "$dependencies"
mv -T -- "$dependencies" "$base/deploy_checks/$release_id.txt"
if [[ -e $base/releases/$release_id ]]; then
    verify_release "$base/releases/$release_id"
else
    mv -- "$stage" "$base/releases/$release_id"
fi
verify_release "$base/releases/$release_id"
if [[ -L $base/current ]]; then
    previous=$(readlink "$base/current")
    [[ $previous =~ ^releases/[a-f0-9]{64}$ ]]
    printf "%s\n" "$previous" > "$base/previous_release.new"
    mv -- "$base/previous_release.new" "$base/previous_release"
fi
ln -s -- "releases/$release_id" "$base/.current.$$"
mv -Tf -- "$base/.current.$$" "$base/current"
printf "DEPLOYED_RELEASE=%s\nSTARTED=0\n" "$release_id"
' "$base" "$staged_upload" "$checksum" "$release_id"
    cleanup_upload
    staged_upload=
    trap - EXIT
}
deploy
