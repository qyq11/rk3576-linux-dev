#!/usr/bin/env bash
set -euo pipefail
test "$(id -un)" = qyq
test "${WSL_DISTRO_NAME:-}" = 'RK3576-Build-22.04'
stage=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/system_image_20261030
sdk=/home/qyq/TaishanPi-3-Linux
install -m 755 "$stage/repo" /home/qyq/bin/repo
export PATH="/home/qyq/bin:$PATH"
export REPO_URL=https://cnb.cool/jlckfb/git-repo
export REPO_REV=main
export GIT_LFS_SKIP_SMUDGE=1
export LANG=en_US.UTF-8
git config --global user.name 'RK3576 build'
git config --global user.email 'rk3576-build@localhost'
git config --global init.defaultBranch main
git config --global core.autocrlf false
git lfs install --skip-smudge
cd "$sdk"
repo init -u https://github.com/jlckfb/TaishanPi-manifests.git \
    -b 36d3aadff62760df0a12ebcfcfa9d1373ef66654 \
    --manifest-depth=1 --depth=1 --no-clone-bundle < /dev/null
repo sync -c --no-clone-bundle -j4 --fail-fast
repo manifest -r -o "$stage/manifest_pinned.xml"
repo list > "$stage/sdk_projects.txt"
printf 'SDK_SOURCE_SYNC_EXIT=0\n'
