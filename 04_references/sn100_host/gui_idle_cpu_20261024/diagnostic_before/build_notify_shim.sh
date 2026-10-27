#!/usr/bin/env bash
set -Eeuo pipefail
root=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
out="$root/temp/sn100_host/o09_loopback/cpu_before"
g++ -shared -fPIC -O2 -std=c++14 -Wall -Wextra "$out/notify_shim.cpp" -o "$out/notify_shim.so" $(pkg-config --cflags --libs Qt5Widgets) -ldl -pthread
