#!/usr/bin/env bash
set -Eeuo pipefail
root='/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux'
task_dir="$root/temp/sn100_host/formal_merge_20261104_01"
[[ $(cat "$task_dir/build_02.exit") == 0 ]] || exit 2
python3 "$root/06_programs/sn100_host/shell/check_arm64.py" "$task_dir/bin_02/sn100_shell" --additional-binary "$task_dir/bin_02/sn100_report_renderer" --sysroot "$task_dir/sysroot" --output-dir "$task_dir/dependencies" > "$task_dir/dependency.stdout" 2> "$task_dir/dependency.stderr"
printf '0\n' > "$task_dir/dependency.exit"
python3 "$root/06_programs/sn100_host/shell/package_arm64.py" package --binary "$task_dir/bin_02/sn100_shell" --sysroot "$task_dir/sysroot" --snapshot "$task_dir/package_source_snapshot.json" --dependencies "$task_dir/dependencies/dependency_report.json" --output "$task_dir/packages" > "$task_dir/package.stdout" 2> "$task_dir/package.stderr"
printf '0\n' > "$task_dir/package.exit"
