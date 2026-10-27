#!/usr/bin/env bash
# Recreate offline inspection inputs and private SQLite runtime; never contact RK.
set -Eeuo pipefail
[[ $# == 1 && $1 == /* ]] || { echo 'Usage: prepare_runtime.sh ABSOLUTE_SYSROOT' >&2; exit 2; }
[[ $EUID != 0 ]] || { echo 'Run as the ordinary WSL user.' >&2; exit 2; }
module_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$module_dir/../../.." && pwd)
archive=$root/07_backups/qt_environment/sn100_rk_runtime
work=${SN100_RK_WORK_DIR:-$root/temp/sn100_host/rk_prepare}
[[ $work == /* ]] || { echo 'SN100_RK_WORK_DIR must be absolute.' >&2; exit 2; }
sysroot=$1
mkdir -p "$work" "$archive" "$sysroot"
exec 8>"$work/runtime.lock"
flock -n 8 || { echo 'Runtime inputs are in use.' >&2; exit 1; }

# SN100已有独立归档的加载器，仅用于电脑端依赖检查，不安装到RK。
# 示例目录已删除；缺少正式归档时明确停止，不能依赖临时旧副本恢复。
[[ -f $archive/board_loader.tar.gz && -f $archive/loader.SHA256SUMS ]] || {
    echo 'Archived board loader is absent; restore the known input, do not substitute host libc.' >&2; exit 1;
}
(cd "$archive" && sha256sum -c loader.SHA256SUMS)

# Noble main supplies the missing libsqlite3 provider. Pin the downloaded file
# with its official package-index SHA; later runs use the archived input only.
if [[ ! -f $archive/sqlite.SHA256SUMS ]]; then
    url=https://ports.ubuntu.com/ubuntu-ports/dists/noble/main/binary-arm64/Packages.xz
    curl -fL --retry 2 "$url" -o "$work/main_Packages.xz"
    xz -dc "$work/main_Packages.xz" > "$work/main_Packages"
    awk 'BEGIN { RS=""; FS="\n" } $1=="Package: libsqlite3-0" { print; found=1; exit } END { if (!found) exit 1 }' \
        "$work/main_Packages" > "$archive/sqlite_package_index.txt"
    filename=$(awk '$1=="Filename:" {print $2}' "$archive/sqlite_package_index.txt")
    checksum=$(awk '$1=="SHA256:" {print $2}' "$archive/sqlite_package_index.txt")
    [[ $filename == pool/main/s/sqlite3/*.deb && $checksum =~ ^[a-f0-9]{64}$ ]] || { echo 'Unexpected official SQLite index.' >&2; exit 1; }
    name=${filename##*/}
    curl -fL --retry 2 "https://ports.ubuntu.com/ubuntu-ports/$filename" -o "$archive/$name.new"
    printf '%s  %s\n' "$checksum" "$name.new" | (cd "$archive" && sha256sum -c -)
    mv -- "$archive/$name.new" "$archive/$name"
    printf '%s  %s\n' "$checksum" "$name" > "$archive/sqlite.SHA256SUMS"
fi
(cd "$archive" && sha256sum -c sqlite.SHA256SUMS)

base=$root/07_backups/qt_environment/sn100_base_arm64
sql=$root/07_backups/qt_environment/sn100_arm64
(cd "$base" && sha256sum -c SHA256SUMS)
(cd "$sql" && sha256sum -c SHA256SUMS)
dpkg-deb -x "$base/qtbase5-dev_5.15.13+dfsg-1ubuntu1_arm64.deb" "$sysroot"
tar -xzf "$base/runtime_libs.tar.gz" -C "$sysroot"
for package in "$sql"/*.deb "$archive"/*.deb; do dpkg-deb -x "$package" "$sysroot"; done
tar -xzf "$archive/board_loader.tar.gz" -C "$sysroot"
report_archive=$root/07_backups/qt_environment/sn100_report_arm64
[[ -f $report_archive/SHA256SUMS ]] || { echo 'Archived ARM64 report inputs are required.' >&2; exit 1; }
(cd "$report_archive" && sha256sum -c SHA256SUMS)
: > "$sysroot/report_private_libraries.txt.new"
while read -r checksum package; do
    [[ $checksum =~ ^[a-f0-9]{64}$ && $package == *.deb && $package != */* ]] || { echo 'Invalid report archive manifest.' >&2; exit 1; }
    dpkg-deb -x "$report_archive/$package" "$sysroot"
    # Record provenance-qualified runtime candidates. Only inspected closure
    # providers from these packages may become additional private libraries.
    dpkg-deb --fsys-tarfile "$report_archive/$package" | tar -tf - \
        | sed -n 's@^\./\(.*lib[^/]*\.so\.[0-9][^/]*\)$@/\1@p' >> "$sysroot/report_private_libraries.txt.new"
done < "$report_archive/SHA256SUMS"
sort -u "$sysroot/report_private_libraries.txt.new" > "$sysroot/report_private_libraries.txt"
rm -f -- "$sysroot/report_private_libraries.txt.new"
font_archive=$root/07_backups/qt_environment/wsl_amd64
font_package=$(awk '$2 ~ /^packages\/fonts-noto-cjk_.*_all\.deb$/ {print $2}' "$font_archive/SHA256SUMS")
[[ -n $font_package && $font_package != *$'\n'* ]] || { echo 'A unique archived Noto CJK package is required.' >&2; exit 1; }
awk -v name="$font_package" '$2 == name {print}' "$font_archive/SHA256SUMS" | (cd "$font_archive" && sha256sum -c -)
dpkg-deb -x "$font_archive/$font_package" "$sysroot"
platform_archive=$root/07_backups/qt_environment/sn100_report_runtime_arm64
(cd "$platform_archive" && sha256sum -c SHA256SUMS)
while read -r checksum package; do
    [[ $checksum =~ ^[a-f0-9]{64}$ && $package != */* ]] || { echo 'Invalid offscreen runtime archive manifest.' >&2; exit 1; }
    case $package in
        libqt5gui5t64_5.15.13+dfsg-1ubuntu1_arm64.deb)
            # Preserve board-derived QtGui. Take only the matching plugin and notice.
            dpkg-deb --fsys-tarfile "$platform_archive/$package" | tar -x -C "$sysroot" \
                ./usr/lib/aarch64-linux-gnu/qt5/plugins/platforms/libqoffscreen.so \
                ./usr/share/doc/libqt5gui5t64/copyright ;;
        libfontconfig1_*_arm64.deb|libexpat1_*_arm64.deb)
            dpkg-deb -x "$platform_archive/$package" "$sysroot"
            dpkg-deb --fsys-tarfile "$platform_archive/$package" | tar -tf - \
                | sed -n 's@^\./\(.*lib[^/]*\.so\.[0-9][^/]*\)$@/\1@p' >> "$sysroot/report_private_libraries.txt" ;;
        *) echo 'Unexpected offscreen runtime package.' >&2; exit 1 ;;
    esac
done < "$platform_archive/SHA256SUMS"

# Restore only the signed-index-pinned QtSvg 5.15.13 inputs. These extend the
# private sysroot for the original sample combo-box SVG arrow; nothing is
# installed into WSL or the board filesystem.
svg_archive=$root/07_backups/qt_environment/sn100_samples_arm64
[[ -f $svg_archive/SHA256SUMS ]] || { echo 'Archived matching ARM64 QtSvg inputs are required.' >&2; exit 1; }
(cd "$svg_archive" && sha256sum -c SHA256SUMS)
declare -A svg_packages=()
while read -r checksum package; do
    case $package in
        libqt5svg5_5.15.13-1_arm64.deb)
            expected=813202055ca9743206d33e955cac3152a3bac7abbbc8254e01c18feda6e39d0d ;;
        libqt5svg5-dev_5.15.13-1_arm64.deb)
            expected=690245647bb8fd8ce21feaf8bece72c4cf64c85747fe6baa96dc20dd4eb95c1d ;;
        *) echo 'Unexpected QtSvg archive package.' >&2; exit 1 ;;
    esac
    [[ $checksum == "$expected" && ! ${svg_packages[$package]+present} ]] || { echo 'Invalid or duplicate QtSvg archive entry.' >&2; exit 1; }
    [[ $(dpkg-deb -f "$svg_archive/$package" Version) == 5.15.13-1 && $(dpkg-deb -f "$svg_archive/$package" Architecture) == arm64 ]] || { echo 'QtSvg archive version or architecture mismatch.' >&2; exit 1; }
    svg_packages[$package]=1
    dpkg-deb -x "$svg_archive/$package" "$sysroot"
done < "$svg_archive/SHA256SUMS"
[[ ${#svg_packages[@]} == 2 ]] || { echo 'Both matching QtSvg runtime and development inputs are required.' >&2; exit 1; }
for provider in \
    /usr/lib/aarch64-linux-gnu/libQt5Svg.so.5.15.13 \
    /usr/lib/aarch64-linux-gnu/qt5/plugins/imageformats/libqsvg.so \
    /usr/lib/aarch64-linux-gnu/qt5/plugins/iconengines/libqsvgicon.so; do
    [[ -f $sysroot$provider ]] || { echo "Archived QtSvg provider is absent: $provider" >&2; exit 1; }
    printf '%s\n' "$provider" >> "$sysroot/report_private_libraries.txt"
done
[[ -f $sysroot/usr/include/aarch64-linux-gnu/qt5/QtSvg/QSvgRenderer && -f $sysroot/usr/share/doc/libqt5svg5/copyright ]] || { echo 'Archived QtSvg development files or notice are absent.' >&2; exit 1; }
sort -u -o "$sysroot/report_private_libraries.txt" "$sysroot/report_private_libraries.txt"

inspection_archive=$root/07_backups/qt_environment/sn100_report_inspection_arm64
(cd "$inspection_archive" && sha256sum -c SHA256SUMS)
while read -r checksum package; do
    [[ $checksum =~ ^[a-f0-9]{64}$ && $package == libc6_2.39-0ubuntu8.9_arm64.deb ]] || { echo 'Unexpected board-matching inspection input.' >&2; exit 1; }
    # Kerberos needs the board's resolver provider. This is host inspection
    # input only: package_arm64 never includes this libc-package path.
    dpkg-deb --fsys-tarfile "$inspection_archive/$package" | tar -x -C "$sysroot" \
        ./usr/lib/aarch64-linux-gnu/libresolv.so.2
done < "$inspection_archive/SHA256SUMS"
# The original runtime archive kept versioned Qt files but omitted some SONAME
# aliases. Recreate aliases only from the ELF's own SONAME inside this sysroot.
for library in "$sysroot"/usr/lib/aarch64-linux-gnu/libQt5*.so.5.15.13; do
    soname=$(aarch64-linux-gnu-readelf -d "$library" | sed -n 's/.*(SONAME).*\[\([^]]*\)\].*/\1/p')
    [[ $soname =~ ^libQt5[A-Za-z0-9]+\.so\.5$ ]] || { echo 'Unexpected Qt SONAME.' >&2; exit 1; }
    if [[ ! -e ${library%/*}/$soname ]]; then ln -s -- "${library##*/}" "${library%/*}/$soname"; fi
done
printf 'RUNTIME_SYSROOT=%s\n' "$sysroot"
