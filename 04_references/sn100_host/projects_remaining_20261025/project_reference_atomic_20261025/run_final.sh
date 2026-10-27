#!/usr/bin/env bash
set -u
root=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
base=$root/temp/sn100_host/projects_remaining_20261025/project_reference_atomic_20261025
evidence=$root/04_references/sn100_host/projects_remaining_20261025/project_reference_atomic_20261025
mkdir -p "$base/final_build" "$base/final_runtime"
cd "$base/final_build" || exit 2
unset QMAKESPEC QMAKEPATH QMAKEFEATURES QT_PLUGIN_PATH QML2_IMPORT_PATH
printf '%s\n' '/usr/lib/qt5/bin/qmake <repo>/06_programs/sn100_host/tests/project_reference_atomic_test.pro -spec linux-g++ CONFIG+=release CONFIG-=debug' 'make -j2' 'SN100_PROJECT_ATOMIC_ROOT=<private>/final_runtime ./project_reference_atomic_test -o <evidence>/final_test.xml,xml -o -,txt' > "$evidence/final_commands.txt"
/usr/lib/qt5/bin/qmake "$root/06_programs/sn100_host/tests/project_reference_atomic_test.pro" -spec linux-g++ CONFIG+=release CONFIG-=debug > "$evidence/final_build.log" 2>&1
qmake_exit=$?
if [[ $qmake_exit -eq 0 ]]; then make -j2 >> "$evidence/final_build.log" 2>&1; make_exit=$?; else make_exit=125; fi
printf 'qmake_exit=%s\nmake_exit=%s\n' "$qmake_exit" "$make_exit" > "$evidence/final_build_receipt.txt"
if [[ $make_exit -ne 0 ]]; then tail -n 18 "$evidence/final_build.log"; exit "$make_exit"; fi
SN100_PROJECT_ATOMIC_ROOT="$base/final_runtime" ./project_reference_atomic_test -o "$evidence/final_test.xml,xml" -o -,txt > "$evidence/final_test.log" 2>&1
test_exit=$?
printf 'test_exit=%s\n' "$test_exit" > "$evidence/final_test_receipt.txt"
cat "$evidence/final_build_receipt.txt" "$evidence/final_test_receipt.txt"
tail -n 23 "$evidence/final_test.log"
exit "$test_exit"
