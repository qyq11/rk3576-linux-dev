#!/usr/bin/env bash
set -Eeuo pipefail
work=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$work/../../.." && pwd)
module=$root/06_programs/sn100_host/shell
mkdir -p "$work/build" "$work/packages"
python3 "$module/package_arm64.py" snapshot --output "$work/source_snapshot.json"
export SN100_ARM64_SYSROOT=$work/sysroot SN100_ARM64_BUILD_DIR=$work/arm64_build_02 SN100_JOBS=2 RK_BUILD_TYPE=Release
set +e
bash "$module/build.sh" "$work/build/sn100_shell" > "$work/build.log" 2>&1
result=$?
set -e
printf '%s\n' "$result" > "$work/build.exit"
tail -n 8 "$work/build.log"
[[ $result == 0 ]] || exit "$result"
python3 "$module/check_arm64.py" "$work/build/sn100_shell" --additional-binary "$work/build/sn100_report_renderer" \
    --sysroot "$work/sysroot" --output-dir "$work/dependencies" > "$work/dependencies.log" 2>&1
printf '0\n' > "$work/dependencies.exit"
python3 "$module/package_arm64.py" package --binary "$work/build/sn100_shell" --sysroot "$work/sysroot" \
    --snapshot "$work/source_snapshot.json" --dependencies "$work/dependencies/dependency_report.json" \
    --output "$work/packages" > "$work/package.log" 2>&1
printf '0\n' > "$work/package.exit"
cat "$work/package.log"
