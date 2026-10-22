#!/usr/bin/env bash
# Run in WSL. Each program supplies build.sh OUTPUT_FILE.
set -Eeuo pipefail
export LC_ALL=C

usage() {
    cat <<'HELP'
Usage: bash rk.sh PROGRAM [all|build|deploy|run] [-- PROGRAM_ARGS...]
  all (default): build, check, upload, run
  build:         cross-compile and inspect only
  deploy:        rebuild, check and upload
  run:           run the already deployed program
Environment:
  RK_HOST          lckfb@192.168.100.123
  RK_REMOTE_ROOT   /home/rkshare/programs
  RK_RUN_TIMEOUT   30 (seconds; 0 disables the limit)
HELP
}

die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
if [[ ${1:-} == --help || ${1:-} == -h ]]; then usage; exit 0; fi
[[ $# -gt 0 ]] || { usage; exit 2; }
program=$1
shift
[[ $program =~ ^[a-z][a-z0-9_]*$ ]] || die 'Use a program name containing lowercase letters, digits and underscores.'
action=all
if [[ $# -gt 0 && $1 != -- ]]; then action=$1; shift; fi
case $action in all|build|deploy|run) ;; *) usage; exit 2 ;; esac
if [[ $# -gt 0 ]]; then
    [[ $1 == -- ]] || die 'Put -- before program arguments.'
    shift
fi
[[ $action == all || $action == run || $# -eq 0 ]] || die 'Program arguments require all or run.'

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$script_dir/.." && pwd)
program_dir=$script_dir/$program
[[ -f $program_dir/build.sh ]] || die "Missing $program_dir/build.sh"
host=${RK_HOST:-lckfb@192.168.100.123}
remote_root=${RK_REMOTE_ROOT:-/home/rkshare/programs}
remote_root=${remote_root%/}
run_timeout=${RK_RUN_TIMEOUT:-30}
[[ $host =~ ^[a-zA-Z0-9_][a-zA-Z0-9_.@-]*$ ]] || die 'Invalid RK_HOST.'
[[ $remote_root =~ ^/[a-zA-Z0-9_./-]+$ ]] || die 'RK_REMOTE_ROOT must be an absolute path without spaces or shell characters.'
case $remote_root/ in *'/../'*|*'/./'*|*'//'*) die 'RK_REMOTE_ROOT must not contain empty, . or .. components.' ;; esac
[[ $run_timeout =~ ^[0-9]+$ ]] || die 'RK_RUN_TIMEOUT must be a nonnegative integer.'

build_dir=$root/temp/$program/build
logs_dir=$root/temp/$program/logs
mkdir -p -- "$build_dir" "$logs_dir"
command -v flock >/dev/null || die 'flock is required in WSL.'
exec 9>"$root/temp/$program/workflow.lock"
flock -n 9 || die "Another workflow is already using $program."
log_file=$logs_dir/$(date +%Y%m%d_%H%M%S)_${action}_$$.log
exec > >(tee -a "$log_file") 2>&1
binary=$build_dir/$program
remote_dir=$remote_root/$program
staged=
ssh_options=(-o BatchMode=yes -o ConnectTimeout=6 -o ServerAliveInterval=5 -o ServerAliveCountMax=2)

# Quote every argument for the target user's Bash login shell. Keep stdin free
# for programs that consume input; pass the script to bash -c, not via stdin.
remote() {
    local command
    printf -v command '%q ' bash -c "$1" -- "${@:2}"
    ssh -T "${ssh_options[@]}" "$host" "$command"
}

finish() {
    local status=$?
    trap - EXIT
    if [[ -n $staged ]]; then
        remote 'rm -f -- "$1"' "$staged" || true
    fi
    printf '\n[exit] %s\n[log] %s\n' "$status" "$log_file"
    exit "$status"
}
trap finish EXIT
printf '[program] %s\n[action] %s\n[log] %s\n' "$program" "$action" "$log_file"

build() {
    command -v aarch64-linux-gnu-gcc >/dev/null || die 'AArch64 Linux GCC is required.'
    printf '\n[build] %s/build.sh\n' "$program_dir"
    # A failed build never reaches deployment, even if an older binary exists.
    rm -f -- "$binary.new"
    bash "$program_dir/build.sh" "$binary.new"
    [[ -s $binary.new ]] || die 'build.sh did not create a nonempty output file.'
    aarch64-linux-gnu-readelf -h "$binary.new" | tee "$build_dir/elf_header.txt"
    grep -q 'Machine:.*AArch64' "$build_dir/elf_header.txt" || die 'The output is not an AArch64 ELF.'
    aarch64-linux-gnu-readelf -l "$binary.new" > "$build_dir/elf_segments.txt"
    mv -f -- "$binary.new" "$binary"
    file "$binary"
    sha256sum "$binary"
}

connect() {
    printf '\n[connect] %s\n' "$host"
    remote 'set -e; test "$(uname -m)" = aarch64; printf "user="; id -un; printf "machine="; uname -m'
}

deploy() {
    local expected dynamic=0
    expected=$(sha256sum "$binary")
    expected=${expected%% *}
    if grep -q INTERP "$build_dir/elf_segments.txt"; then dynamic=1; fi
    printf '\n[upload] %s:%s/%s\n' "$host" "$remote_dir" "$program"
    staged=$(remote 'set -e; mkdir -p -- "$1"; mktemp "$1/.upload.XXXXXXXX"' "$remote_dir")
    [[ $staged == "$remote_dir"/.upload.* && $staged != *$'\n'* ]] || die 'Unexpected remote staging path.'
    scp -B "${ssh_options[@]}" "$binary" "$host:$staged"
    remote '
set -euo pipefail
actual=$(sha256sum "$1")
actual=${actual%% *}
printf "[remote SHA-256] %s\n" "$actual"
test "$actual" = "$3"
chmod 700 -- "$1"
if [[ $4 == 1 ]]; then
    dependencies=$(LC_ALL=C ldd "$1")
    printf "%s\n" "$dependencies"
    if grep -q "not found" <<< "$dependencies"; then exit 1; fi
fi
mv -f -- "$1" "$2"
printf "[deployed] %s\n" "$2"
' "$staged" "$remote_dir/$program" "$expected" "$dynamic"
    staged=
}

run() {
    printf '\n[run] cwd=%s timeout=%ss\n' "$remote_dir" "$run_timeout"
    printf '[args]'; printf ' %q' "$@"; printf '\n'
    remote '
set -euo pipefail
cd -- "$1"
program=$2
limit=$3
shift 3
test -x "./$program"
exec timeout --foreground --signal=TERM --kill-after=3 "$limit" "./$program" "$@"
' "$remote_dir" "$program" "$run_timeout" "$@"
}

case $action in
    build) build ;;
    deploy) build; connect; deploy ;;
    run) connect; run "$@" ;;
    all) build; connect; deploy; run "$@" ;;
esac
