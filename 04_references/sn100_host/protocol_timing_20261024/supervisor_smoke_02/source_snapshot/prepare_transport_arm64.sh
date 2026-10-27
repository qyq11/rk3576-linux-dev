#!/usr/bin/env bash
# Restore only O05 Network build inputs, never alter the shared Qt sysroot.
set -Eeuo pipefail
[[ $EUID != 0 ]] || { echo 'Restore as the ordinary WSL user.' >&2; exit 2; }
module_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$module_dir/../../.." && pwd)
archive=$root/07_backups/qt_environment/sn100_protocol_arm64
destination=$root/temp/sn100_host/protocol/o05_2/arm64_network_sysroot
[[ -f $archive/SHA256SUMS ]] || { echo 'O05 ARM64 Network archived inputs are required.' >&2; exit 1; }
(cd "$archive" && sha256sum -c SHA256SUMS)
mkdir -p "$destination"
while read -r checksum package; do
    [[ $package == *.deb && $package != */* ]] || { echo 'Unexpected archive manifest entry.' >&2; exit 1; }
    dpkg-deb -x "$archive/$package" "$destination"
done < "$archive/SHA256SUMS"
test -f "$destination/usr/lib/aarch64-linux-gnu/libQt5Network.so.5.15.13"
echo "Prepared O05-only Network inputs: $destination"
