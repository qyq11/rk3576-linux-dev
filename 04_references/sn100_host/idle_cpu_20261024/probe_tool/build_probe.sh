#!/usr/bin/env bash
set -Eeuo pipefail
work=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/idle_cpu_20261024/probe_tool
g++ -std=c++14 -O2 -fPIC -shared -Wall -Wextra -Wl,-z,defs \
  "$work/idle_probe.cpp" -o "$work/libsn100_idle_probe.so" $(pkg-config --cflags --libs Qt5Widgets) -ldl
nm -D --defined-only "$work/libsn100_idle_probe.so" | grep -E '_ZN12QApplication4execEv|_ZN7QWidget6updateEv'
file "$work/libsn100_idle_probe.so"
sha256sum "$work/idle_probe.cpp" "$work/libsn100_idle_probe.so"
