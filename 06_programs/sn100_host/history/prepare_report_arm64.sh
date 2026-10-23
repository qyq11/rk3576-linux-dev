#!/usr/bin/env bash
# Restore only archived report cross-build inputs; no network or board I/O.
set -Eeuo pipefail
[[ $EUID != 0 ]] || { echo 'Restore the local sysroot as the ordinary WSL user.' >&2; exit 2; }
export PATH=/usr/local/bin:/usr/bin:/bin
module_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$module_dir/../../.." && pwd)
archive=$root/07_backups/qt_environment/sn100_report_arm64
sysroot=$root/temp/sn100_host/arm64_sysroot
[[ -f $archive/SHA256SUMS ]] || { echo 'Archived O04 ARM64 report inputs are missing.' >&2; exit 1; }
mkdir -p "$sysroot"
(
    cd "$archive"
    sha256sum -c SHA256SUMS
    while read -r checksum package; do
        [[ -n $checksum && $package == *.deb && $package != */* ]] || { echo 'Unexpected package manifest entry.' >&2; exit 1; }
        dpkg-deb -x "$package" "$sysroot"
    done < SHA256SUMS
)
echo "Prepared local report ARM64 inputs: $sysroot"
