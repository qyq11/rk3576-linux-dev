#!/usr/bin/env bash
# WSL workflow for the unified shell. Only explicit remote actions contact RK.
set -Eeuo pipefail
usage() {
    cat <<'HELP'
Usage: bash sn100_host/shell/rk.sh ACTION [ARGS]
  prepare             fresh ARM64 build, offline closure check and release package
  deploy              prepare again, upload/verify immutable release; does not start
  check               target runtime/display/input preflight; does not start
  start [SEC [USER]]   EGLFS on VT7; default 1800-second soft close, Guest
  status | stop       inspect or request guarded close of the exact owned process
  collect             download only allowlisted logs and actual exit results
Environment: RK_HOST=lckfb@192.168.100.123
             RK_REMOTE_ROOT=/home/rkshare/programs (app subdirectory: sn100_shell)
             SN100_JOBS=2; RK_BUILD_TYPE=Release
USER is an explicit anonymous offlineSend/Test/Check/Engineer/Admin session.
SEC=0 disables the soft deadline. Dirty/busy guards may leave close pending.
No action installs system libraries, enables autostart or connects an instrument.
HELP
}
if [[ ${1:-} == -h || ${1:-} == --help ]]; then usage; exit 0; fi
action=${1:-prepare}
[[ $# == 0 ]] || shift
case $action in prepare|deploy|check|start|status|stop|collect) ;; *) usage >&2; exit 2 ;; esac
[[ $action == start || $# == 0 ]] || { usage >&2; exit 2; }
[[ $EUID != 0 && $(uname -m) == x86_64 ]] || { echo 'Run in WSL as qyq.' >&2; exit 2; }
module_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$module_dir/../../.." && pwd)
work=${SN100_RK_WORK_DIR:-$root/temp/sn100_host/rk_prepare}
[[ $work == /* ]] || { echo 'SN100_RK_WORK_DIR must be absolute.' >&2; exit 2; }
host=${RK_HOST:-lckfb@192.168.100.123}
remote_root=${RK_REMOTE_ROOT:-/home/rkshare/programs}
remote_root=${remote_root%/}
[[ $host =~ ^[a-zA-Z0-9_][a-zA-Z0-9_.@-]*$ ]] || { echo 'Invalid RK_HOST.' >&2; exit 2; }
[[ $remote_root =~ ^/[a-zA-Z0-9_./-]+$ ]] || { echo 'Invalid RK_REMOTE_ROOT.' >&2; exit 2; }
case $remote_root/ in *'/../'*|*'/./'*|*'//'*) echo 'Unnormalized RK_REMOTE_ROOT.' >&2; exit 2 ;; esac
base=$remote_root/sn100_shell
mkdir -p "$work/logs" "$work/build" "$work/packages"
exec 7>"$work/workflow.lock"
flock -n 7 || { echo 'Another unified RK workflow is in progress.' >&2; exit 1; }
log=$work/logs/$(date +%Y%m%d_%H%M%S)_${action}_$$.log
exec > >(tee -a "$log") 2>&1
opts=(-o BatchMode=yes -o ConnectTimeout=6 -o ServerAliveInterval=5 -o ServerAliveCountMax=2)
staged_upload=
remote() {
    local command
    printf -v command '%q ' bash -c "$1" -- "${@:2}"
    ssh -T "${opts[@]}" "$host" "$command"
}
board_action() {
    # Check the immutable manifest BEFORE executing its target-side helper.
    remote '
set -euo pipefail
base=$1; shift
release=$(readlink -f "$base/current")
[[ $release == "$base/releases/"* && ${release#"$base/releases/"} =~ ^[a-f0-9]{64}$ ]]
test "$(cat "$release/release_id")" = "${release##*/}"
test "$(sha256sum "$release/SHA256SUMS" | cut -d" " -f1)" = "${release##*/}"
(cd "$release" && sha256sum --quiet -c SHA256SUMS)
exec bash "$release/rk_board.sh" "$1" "$base" "${@:2}"
' "$base" "$@"
}
prepare() {
    local binary=$work/build/sn100_shell
    python3 "$module_dir/package_arm64.py" snapshot --output "$work/source_snapshot.json"
    bash "$module_dir/prepare_runtime.sh" "$work/sysroot"
    SN100_ARM64_SYSROOT=$work/sysroot SN100_ARM64_BUILD_DIR=$work/arm64_build SN100_JOBS=${SN100_JOBS:-2} \
        bash "$module_dir/build.sh" "$binary.new"
    python3 "$module_dir/check_arm64.py" "$binary.new" --additional-binary "$work/build/sn100_report_renderer" --sysroot "$work/sysroot" --output-dir "$work/dependencies"
    mv -f -- "$binary.new" "$binary"
    python3 "$module_dir/package_arm64.py" package --binary "$binary" --sysroot "$work/sysroot" \
        --snapshot "$work/source_snapshot.json" --dependencies "$work/dependencies/dependency_report.json" --output "$work/packages"
    printf 'READY_FOR_TRANSFER=1\nBOARD_RUNTIME_VERIFIED=0\nLOG=%s\n' "$log"
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
case $action in
    prepare) prepare ;;
    deploy) prepare; deploy ;;
    check|status|stop)
        board_action "$action" ;;
    start)
        [[ $# -le 2 ]] || { usage >&2; exit 2; }
        seconds=${1:-1800}; session=${2:-}
        [[ $seconds =~ ^(0|[1-9][0-9]{0,4})$ && $seconds -le 86400 ]] || { echo 'SEC must be 0..86400.' >&2; exit 2; }
        case $session in ''|offlineSend|offlineTest|offlineCheck|offlineEngineer|offlineAdmin) ;; *) echo 'Unknown anonymous session.' >&2; exit 2 ;; esac
        board_action start "$seconds" "$session" ;;
    collect)
        out=$work/collected/$(date +%Y%m%d_%H%M%S)_$$
        mkdir -p "$out"
        board_action collect > "$out/logs.tar.gz.new"
        gzip -t "$out/logs.tar.gz.new"
        mv -- "$out/logs.tar.gz.new" "$out/logs.tar.gz"
        tar -xzf "$out/logs.tar.gz" -C "$out"
        printf 'LOCAL_EVIDENCE=%s\n' "$out"
        if [[ -f $out/result.txt ]]; then cat "$out/result.txt"; fi ;;
esac
