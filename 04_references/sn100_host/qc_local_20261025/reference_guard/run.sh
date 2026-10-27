#!/usr/bin/env bash
set -u
root=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
base=$root/temp/sn100_host/qc_local_20261025/reference_guard
attempt=${1:-2}
mkdir -p "$base/build" "$base/runtime"
cd "$base/build" || exit 2
printf 'attempt=%s\n' "$attempt" >> "$base/commands.txt"
printf '%s\n' '/usr/lib/qt5/bin/qmake <root>/06_programs/sn100_host/tests/calibration_reference_test.pro CONFIG+=release CONFIG-=debug' 'make -j2' >> "$base/commands.txt"
/usr/lib/qt5/bin/qmake "$root/06_programs/sn100_host/tests/calibration_reference_test.pro" CONFIG+=release CONFIG-=debug > "$base/build_$attempt.log" 2>&1
qmake_exit=$?
if [ "$qmake_exit" -eq 0 ]; then make -j2 >> "$base/build_$attempt.log" 2>&1; make_exit=$?; else make_exit=125; fi
printf 'qmake_exit=%s\nmake_exit=%s\n' "$qmake_exit" "$make_exit" > "$base/build_${attempt}_receipt.txt"
cat "$base/build_${attempt}_receipt.txt"
if [ "$make_exit" -ne 0 ]; then tail -40 "$base/build_$attempt.log"; exit "$make_exit"; fi
printf '%s\n' 'SN100_REFERENCE_TEST_ROOT=<base>/runtime ./calibration_reference_test -v1' >> "$base/commands.txt"
SN100_REFERENCE_TEST_ROOT="$base/runtime" ./calibration_reference_test -v1 > "$base/test_$attempt.log" 2>&1
test_exit=$?
printf 'test_exit=%s\n' "$test_exit" > "$base/test_${attempt}_receipt.txt"
cat "$base/test_${attempt}_receipt.txt"
tail -80 "$base/test_$attempt.log"
exit "$test_exit"
