#!/usr/bin/env bash
set -Eeuo pipefail
root=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
cd "$root/temp/sn100_host/samples/wsl_tests"
g++ -std=c++14 -O2 -fPIC -I.ui -I.moc -I"$root/06_programs/sn100_host" -I"$root/06_programs/sn100_host/samples" -I"$root/06_programs/sn100_host/projects" -I"$root/06_programs/sn100_host/ui" -I"$root/06_programs/sn100_host/shell/widgets" -I"$root/06_programs/sn100_host/samples/material" $(pkg-config --cflags Qt5Widgets Qt5Sql Qt5Svg Qt5Test) -c ../edge_probe.cpp -o ../edge_probe.o
input_objects=()
for candidate in .obj/*.o; do
  [[ $candidate == .obj/samples_test.o ]] || input_objects+=("$candidate")
done
g++ -o ../edge_probe ../edge_probe.o "${input_objects[@]}" $(pkg-config --libs Qt5Widgets Qt5Sql Qt5Svg Qt5Test)
exec env QT_QPA_PLATFORM=offscreen SN100_SAMPLE_TEST_ROOT="$root/temp/sn100_host/samples/edge_runtime" ../edge_probe > "$root/04_references/sn100_host/samples_20261024/edge_probe.log" 2>&1

