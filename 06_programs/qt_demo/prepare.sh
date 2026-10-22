#!/usr/bin/env bash
# Run in WSL as qyq. Downloads headers, without installing packages on the RK.
set -Eeuo pipefail
program_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$program_dir/../.." && pwd)
work=$root/temp/qt_demo
archive=$root/07_backups/qt_environment/qt5_arm64
host=${RK_HOST:-lckfb@192.168.100.123}
opts=(-o BatchMode=yes -o ConnectTimeout=6)
mkdir -p "$work/packages" "$work/sysroot" "$work/logs"
exec > >(tee -a "$work/logs/prepare_$(date +%Y%m%d_%H%M%S).log") 2>&1
if [[ -f $archive/SHA256SUMS && -f $archive/runtime_libs.tar.gz ]]; then
    (cd "$archive" && sha256sum -c SHA256SUMS)
    dpkg-deb -x "$archive/qtbase5-dev_5.15.13+dfsg-1ubuntu1_arm64.deb" "$work/sysroot"
    tar -xzf "$archive/runtime_libs.tar.gz" -C "$work/sysroot"
    echo "Prepared archived Qt 5.15.13 cross-build inputs: $work/sysroot"
    exit 0
fi
ssh -T "${opts[@]}" "$host" 'bash -s' <<'REMOTE'
set -Eeuo pipefail
mkdir -p /home/rkshare/programs/qt_demo/setup
cd /home/rkshare/programs/qt_demo/setup
test "$(dpkg-query -W -f='${Version}' libqt5core5t64)" = '5.15.13+dfsg-1ubuntu1'
test -f qtbase5-dev_5.15.13+dfsg-1ubuntu1_arm64.deb || apt-get download qtbase5-dev=5.15.13+dfsg-1ubuntu1
{
  printf '%s\n' usr/lib/aarch64-linux-gnu/libQt5Core.so.5.15.13 usr/lib/aarch64-linux-gnu/libQt5Gui.so.5.15.13 usr/lib/aarch64-linux-gnu/libQt5Widgets.so.5.15.13
  ldd /usr/lib/aarch64-linux-gnu/libQt5Widgets.so.5 | awk '$2 == "=>" && $3 ~ /^\// { sub(/^\//,"",$3); print $3 }'
} | sort -u > runtime_files.txt
tar -C / -chzf runtime_libs.tar.gz -T runtime_files.txt
sha256sum qtbase5-dev_5.15.13+dfsg-1ubuntu1_arm64.deb runtime_libs.tar.gz > SHA256SUMS
uname -a > target_info.txt
dpkg-query -W libqt5core5t64 libqt5gui5t64 libqt5widgets5t64 libegl-mesa0 libgbm1 libgl1-mesa-dri >> target_info.txt
REMOTE
sources=()
for name in qtbase5-dev_5.15.13+dfsg-1ubuntu1_arm64.deb runtime_libs.tar.gz runtime_files.txt SHA256SUMS target_info.txt; do
    sources+=("$host:/home/rkshare/programs/qt_demo/setup/$name")
done
scp -B "${opts[@]}" "${sources[@]}" "$work/packages/"
(cd "$work/packages" && sha256sum -c SHA256SUMS)
dpkg-deb -x "$work/packages/qtbase5-dev_5.15.13+dfsg-1ubuntu1_arm64.deb" "$work/sysroot"
tar -xzf "$work/packages/runtime_libs.tar.gz" -C "$work/sysroot"
test -f "$work/sysroot/usr/include/aarch64-linux-gnu/qt5/QtWidgets/QApplication"
aarch64-linux-gnu-g++ --version | head -1
echo "Prepared Qt 5.15.13 ARM64 headers and matching target libraries: $work/sysroot"
