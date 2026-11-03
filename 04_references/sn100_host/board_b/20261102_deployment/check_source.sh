#!/usr/bin/env bash
set -Eeuo pipefail
root=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
cd "$root"
python3 06_programs/sn100_host/shell/package_arm64.py snapshot --output "$root/temp/sn100_host/board_b/20261102_deploy/current_source_snapshot.json"
cmp 07_backups/sn100_host/reagent_calendar_20261030/source_snapshot.json temp/sn100_host/board_b/20261102_deploy/current_source_snapshot.json
printf 'CURRENT_PACKAGE_INPUTS_UNCHANGED=1\n'
