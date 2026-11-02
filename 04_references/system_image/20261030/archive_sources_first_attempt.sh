#!/usr/bin/env bash
set -euo pipefail
test "$(id -un)" = qyq
test "${WSL_DISTRO_NAME:-}" = 'RK3576-Build-22.04'
project=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
sdk=/home/qyq/TaishanPi-3-Linux
evidence=$project/04_references/system_image/20261030
destination=$project/07_backups/system_image/sources
archive=$destination/official_sdk_36d3aadf_20261030.tar.gz
export PATH="/home/qyq/bin:$PATH"
export LANG=en_US.UTF-8
cd "$sdk"
repo list -p > "$evidence/sdk_project_paths.txt"
: > "$evidence/lfs_files.txt"
while IFS= read -r path; do
    files=$(git -C "$path" lfs ls-files -l)
    if [[ -n "$files" ]]; then
        printf '\nPROJECT=%s\n%s\n' "$path" "$files" | tee -a "$evidence/lfs_files.txt"
        git -C "$path" lfs pull
        git -C "$path" lfs fsck --objects --pointers
    fi
done < "$evidence/sdk_project_paths.txt"
repo manifest -r -o "$evidence/manifest_pinned.xml"
repo forall -c 'git status --porcelain=v1' > "$evidence/official_worktree_status.txt"
test ! -s "$evidence/official_worktree_status.txt"
mkdir -p "$destination"
test ! -e "$archive"
cp "$evidence/manifest_pinned.xml" "$destination/manifest_pinned.xml"
printf 'ARCHIVE_START=%s\n' "$(date --iso-8601=seconds)"
tar --exclude='./.repo' --exclude='.git' --exclude='./output' \
    --exclude='./rockdev' --exclude='./.ccache' -C "$sdk" -cf - . \
    | pigz -1 -p 2 > "$archive.part"
mv "$archive.part" "$archive"
gzip -t "$archive"
cd "$destination"
sha256sum "$(basename "$archive")" manifest_pinned.xml > SHA256SUMS
sha256sum -c SHA256SUMS
printf 'ARCHIVE_END=%s\nOFFICIAL_SOURCE_ARCHIVE_EXIT=0\n' "$(date --iso-8601=seconds)"
