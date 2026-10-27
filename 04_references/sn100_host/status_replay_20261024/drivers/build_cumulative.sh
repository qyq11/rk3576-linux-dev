#!/usr/bin/env bash
set -Eeuo pipefail
root=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
out=$root/temp/sn100_host/status_replay_20261024
cd "$out/shell_test"
cp Makefile Makefile.status_replay
for name in catalog_shell integrated_shell integrated_report; do
    /usr/lib/qt5/bin/qmake -o "Makefile.$name" "$root/06_programs/sn100_host/tests/${name}_test.pro" -spec linux-g++ CONFIG+=release CONFIG-=debug > "$out/logs/${name}_build.log" 2>&1
    python3 -B "$root/temp/sn100_host/catalog_integration/compare_native_flags.py" Makefile.status_replay "Makefile.$name" > "$out/logs/${name}_flags.json"
    make -f "Makefile.$name" -j2 >> "$out/logs/${name}_build.log" 2>&1
done
