#!/usr/bin/env bash
# Run in WSL. SSH stdio transports the GDB protocol without opening a listening TCP port.
set -Eeuo pipefail
mode=${1:-interactive}
[[ $mode == check || $mode == interactive ]] || { echo 'Usage: debug_rk.sh [check|interactive]' >&2; exit 2; }
program_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$program_dir/../.." && pwd)
host=${RK_HOST:-lckfb@192.168.100.123}
[[ $host =~ ^[a-zA-Z0-9_][a-zA-Z0-9_.@-]*$ ]] || exit 2
command -v gdb-multiarch >/dev/null || { echo 'Install gdb-multiarch in WSL first.' >&2; exit 1; }
opts=(-o BatchMode=yes -o ConnectTimeout=6)
work=$root/temp/qt_demo
archive=$root/07_backups/qt_environment/debug_tools
mkdir -p "$work/remote_debug" "$work/sysroot/lib"
if ! ssh -T "${opts[@]}" "$host" test -x /home/rkshare/programs/qt_demo/tools/usr/bin/gdbserver; then
    package=$archive/gdbserver_15.1-1ubuntu1~24.04.1_arm64.deb
    test -f "$package" || { echo 'Missing archived ARM64 gdbserver package.' >&2; exit 1; }
    (cd "$archive" && sha256sum -c SHA256SUMS)
    ssh -T "${opts[@]}" "$host" mkdir -p /home/rkshare/programs/qt_demo/tools/packages
    scp -B "${opts[@]}" "$package" "$host:/home/rkshare/programs/qt_demo/tools/packages/"
    ssh -T "${opts[@]}" "$host" 'dpkg-deb -x /home/rkshare/programs/qt_demo/tools/packages/gdbserver_15.1-1ubuntu1~24.04.1_arm64.deb /home/rkshare/programs/qt_demo/tools'
fi
RK_BUILD_TYPE=Debug bash "$program_dir/../rk.sh" qt_demo deploy
scp -B "${opts[@]}" "$host:/lib/ld-linux-aarch64.so.1" "$work/sysroot/lib/"
scp -B "${opts[@]}" "$host:/usr/lib/aarch64-linux-gnu/libQt5Widgets.so.5" "$work/sysroot/lib/aarch64-linux-gnu/"
stamp=$(date +%Y%m%d_%H%M%S)_$$
out=$work/remote_debug/$stamp
run=/home/rkshare/programs/qt_demo/debug_runs/$stamp
mkdir -p "$out"
seconds=0
[[ $mode != check ]] || seconds=10
ssh -T "${opts[@]}" "$host" bash -s -- "$run" "$seconds" <<'REMOTE'
set -Eeuo pipefail
mkdir -p "$1/runtime"
chmod 700 "$1/runtime"
cat > "$1/server.sh" <<'SERVER'
#!/usr/bin/env bash
set -Eeuo pipefail
run=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd /home/rkshare/programs/qt_demo
export QT_QPA_PLATFORM=offscreen
export XDG_RUNTIME_DIR=$run/runtime
exec tools/usr/bin/gdbserver --once - ./qt_demo --timeout "$1" --state "$run/state.json" --snapshot "$run/render.png"
SERVER
REMOTE
cat > "$out/session.gdb" <<GDB
set pagination off
set confirm off
set debuginfod enabled off
set print pretty on
set auto-solib-add off
set sysroot $work/sysroot
set solib-search-path $work/sysroot/lib/aarch64-linux-gnu:$work/sysroot/usr/lib/aarch64-linux-gnu
file $work/build/qt_demo
target remote | ssh -T -o BatchMode=yes -o ConnectTimeout=6 $host bash $run/server.sh $seconds
break main
GDB
if [[ $mode == check ]]; then
    cat >> "$out/session.gdb" <<GDB
continue
sharedlibrary Qt5
printf "\\nRK_REMOTE_BREAKPOINT_REACHED\\n"
print argc
bt
next
printf "\\nRK_REMOTE_SINGLE_STEP_COMPLETE\\n"
shell ssh -T -o BatchMode=yes $host ps -C gdbserver,qt_demo -o comm=,rss=,pcpu=,stat=
continue
quit
GDB
    timeout --foreground --signal=TERM --kill-after=3 60 gdb-multiarch --batch -x "$out/session.gdb" 2>&1 | tee "$out/debugger.log"
    scp -B "${opts[@]}" "$host:$run/state.json" "$out/"
    scp -B "${opts[@]}" "$host:$run/render.png" "$out/"
else
    echo 'Remote target uses offscreen rendering. This session does not validate physical HDMI or USB input.'
    echo 'At the GDB prompt: continue, next, print VARIABLE, bt, continue, quit.'
    gdb-multiarch -x "$out/session.gdb"
fi
echo "REMOTE_DEBUG_EVIDENCE=$out"
