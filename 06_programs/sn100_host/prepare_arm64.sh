#!/usr/bin/env bash
# Restore known Qt ARM64 inputs locally. No SSH or board changes are performed.
set -Eeuo pipefail
[[ $EUID != 0 ]] || { echo 'Run as the ordinary WSL user.' >&2; exit 2; }
export PATH=/usr/local/bin:/usr/bin:/bin
program_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$program_dir/../.." && pwd)
base=$root/07_backups/qt_environment/qt5_arm64
archive=$root/07_backups/qt_environment/sn100_arm64
work=$root/temp/sn100_host
sysroot=$work/arm64_sysroot
[[ -f $base/SHA256SUMS && -f $base/runtime_libs.tar.gz ]] || { echo 'The previously archived Qt ARM64 inputs are required; no board fallback is allowed.' >&2; exit 1; }
mkdir -p "$archive" "$sysroot"
if [[ ! -f $archive/SHA256SUMS ]]; then
    index=$work/arm64_Packages.xz
    [[ -f $index ]] || curl -fL --retry 2 https://ports.ubuntu.com/ubuntu-ports/dists/noble/universe/binary-arm64/Packages.xz -o "$index"
    xz -dc "$index" > "$work/arm64_Packages"
    : > "$archive/package_index.txt"
    : > "$archive/SHA256SUMS.new"
    for package in libqt5sql5t64 libqt5sql5-sqlite; do
        block=$work/"$package.index"
        awk -v package="$package" 'BEGIN { RS=""; FS="\n" } $1 == "Package: " package { print; found=1; exit } END { if (!found) exit 1 }' "$work/arm64_Packages" > "$block"
        grep -qx 'Version: 5.15.13+dfsg-1ubuntu1' "$block"
        filename=$(awk '$1=="Filename:" { print $2 }' "$block")
        checksum=$(awk '$1=="SHA256:" { print $2 }' "$block")
        name=$(basename -- "$filename")
        curl -fL --retry 2 "https://ports.ubuntu.com/ubuntu-ports/$filename" -o "$archive/$name"
        printf '%s  %s\n' "$checksum" "$name" >> "$archive/SHA256SUMS.new"
        cat "$block" >> "$archive/package_index.txt"
        printf '\n' >> "$archive/package_index.txt"
    done
    (cd "$archive" && sha256sum -c SHA256SUMS.new)
    mv "$archive/SHA256SUMS.new" "$archive/SHA256SUMS"
fi
(cd "$base" && sha256sum -c SHA256SUMS)
(cd "$archive" && sha256sum -c SHA256SUMS)
dpkg-deb -x "$base/qtbase5-dev_5.15.13+dfsg-1ubuntu1_arm64.deb" "$sysroot"
tar -xzf "$base/runtime_libs.tar.gz" -C "$sysroot"
for package in "$archive"/*.deb; do dpkg-deb -x "$package" "$sysroot"; done
echo "Prepared local ARM64 inputs: $sysroot"

