#!/usr/bin/env bash
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
test "$(id -u)" = 0
test "${WSL_DISTRO_NAME:-}" = 'RK3576-Build-22.04'
. /etc/os-release
test "$VERSION_ID" = '22.04'

# 仅配置本次新建的编译环境，下载和编译由普通用户qyq执行。
if ! id qyq >/dev/null 2>&1; then
    useradd -m -s /bin/bash qyq
fi
cat > /etc/apt/sources.list <<'EOF'
deb https://mirrors.ustc.edu.cn/ubuntu/ jammy main restricted universe multiverse
deb https://mirrors.ustc.edu.cn/ubuntu/ jammy-updates main restricted universe multiverse
deb https://mirrors.ustc.edu.cn/ubuntu/ jammy-security main restricted universe multiverse
deb https://mirrors.ustc.edu.cn/ubuntu/ jammy-backports main restricted universe multiverse
EOF
apt-get update
apt-get install -y --no-install-recommends \
    ca-certificates sudo git git-lfs python3 python3-pip python3-requests \
    python3-pyelftools python2 build-essential rsync bc bison flex libssl-dev \
    libncurses5-dev libncurses5 libelf-dev liblz4-tool device-tree-compiler \
    u-boot-tools cpio unzip zip file fakeroot qemu-user-static binfmt-support \
    debootstrap curl wget pigz xz-utils ccache cmake patchelf chrpath gawk \
    texinfo diffstat time locales tzdata whiptail expect ruby pv aria2 kmod \
    openssh-client libgmp-dev libmpc-dev dpkg-dev dosfstools mtools parted \
    gcc-aarch64-linux-gnu g++-aarch64-linux-gnu libgtk2.0-dev libxxf86vm1

# 镜像生成需要挂载临时文件系统；授权只存在于本次专用环境。
printf 'qyq ALL=(ALL) NOPASSWD: ALL\n' > /etc/sudoers.d/rk3576-build
chmod 440 /etc/sudoers.d/rk3576-build
visudo -cf /etc/sudoers.d/rk3576-build
printf '[user]\ndefault=qyq\n' > /etc/wsl.conf
locale-gen en_US.UTF-8
update-binfmts --enable qemu-aarch64
mkdir -p /home/qyq/TaishanPi-3-Linux /home/qyq/bin
chown qyq:qyq /home/qyq/TaishanPi-3-Linux /home/qyq/bin
dpkg-query -W -f='${binary:Package}\t${Version}\n'
printf 'BUILD_HOST_PREPARE_EXIT=0\n'
