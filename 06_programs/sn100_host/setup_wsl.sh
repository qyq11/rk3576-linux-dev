#!/usr/bin/env bash
# Native WSL development packages only; never installs anything on the RK.
set -Eeuo pipefail
[[ $EUID == 0 ]] || { echo 'Run this setup through sudo or wsl.exe -u root; build/run as qyq.' >&2; exit 2; }
program_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$program_dir/../.." && pwd)
archive=$root/07_backups/qt_environment/wsl_amd64
[[ $(dpkg --print-architecture) == amd64 ]] || { echo 'This setup targets the amd64 WSL distribution.' >&2; exit 2; }
packages=(g++ make gdb pkg-config qtbase5-dev qtbase5-dev-tools qt5-qmake qttools5-dev-tools qtcreator libqt5sql5-sqlite libqt5svg5-dev fonts-noto-cjk xdotool time)
mkdir -p "$archive/packages/partial" "$root/temp/sn100_host/logs"
exec > >(tee "$root/temp/sn100_host/logs/wsl_setup.log") 2>&1
if [[ ! -f $archive/packages_before.tsv ]]; then
    dpkg-query -W -f='${binary:Package}\t${Version}\t${Architecture}\n' > "$archive/packages_before.tsv"
    apt-mark showmanual > "$archive/manual_before.txt"
fi
apt-get update
apt-get -s --no-upgrade --no-remove --no-install-recommends install "${packages[@]}" > "$archive/install_preview.txt"
cat "$archive/install_preview.txt"
# Do not silently expand this task into upgrading the existing WSL environment.
grep -Eq '^0 upgraded, [0-9]+ newly installed, 0 to remove' "$archive/install_preview.txt" || {
    echo 'The package solver would upgrade/remove existing packages; inspect the preview first.' >&2; exit 1;
}
apt-get --print-uris --yes --no-upgrade --no-remove --no-install-recommends install "${packages[@]}" > "$archive/package_sources.txt"
apt-get --yes --no-upgrade --no-remove --no-install-recommends \
    -o Dir::Cache::archives="$archive/packages" \
    -o APT::Keep-Downloaded-Packages=true install "${packages[@]}"
dpkg-query -W -f='${binary:Package}\t${Version}\t${Architecture}\n' > "$archive/packages_after.tsv"
apt-mark showmanual > "$archive/manual_after.txt"
dpkg-query -W "${packages[@]}" > "$archive/requested_packages.tsv"
(cd "$archive" && find packages -maxdepth 1 -type f -name '*.deb' -print0 | sort -z | xargs -0 -r sha256sum > SHA256SUMS)
/usr/lib/qt5/bin/qmake -v
echo 'WSL native Qt packages installed; use wsl.sh as the ordinary qyq user.'
