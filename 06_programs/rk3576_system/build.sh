#!/usr/bin/env bash
set -euo pipefail

# 在专用WSL内编译完整系统，并复制到调用者指定的绝对文件路径。
if [[ $# != 1 || $1 != /* ]]; then
    printf 'Usage: bash build.sh /absolute/path/update.img\n' >&2
    exit 2
fi
if [[ ${WSL_DISTRO_NAME:-} != RK3576-Build-22.04 || $(id -un) != qyq ]]; then
    printf '请在RK3576-Build-22.04中以普通用户qyq执行。\n' >&2
    exit 2
fi
sdk=${RK_SYSTEM_SDK_DIR:-/home/qyq/TaishanPi-3-Linux}
entry_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
output=$1
cpuset=${RK_SYSTEM_CPUSET:-0-7}
mirror=${RK_SYSTEM_UBUNTU_MIRROR:-mirrors.ustc.edu.cn}
[[ $mirror =~ ^[a-zA-Z0-9.-]+$ ]] || {
    printf 'Ubuntu软件源需要填写主机名。\n' >&2
    exit 2
}
test -f "$sdk/device/rockchip/common/scripts/build.sh"
test ! -e "$output" || {
    printf '输出文件已经存在，请使用新的输出路径：%s\n' "$output" >&2
    exit 2
}
mkdir -p "$(dirname "$output")"
export RK_SYSTEM_EVIDENCE_DIR="${RK_SYSTEM_EVIDENCE_DIR:-$(dirname "$output")/checks}"
cd "$sdk"
printf 'SDK=%s\nUSER=%s\nCPUSET=%s\n' "$sdk" "$(id -un)" "$cpuset"
bash "$entry_dir/prepare_rt_kernel.sh"
python3 "$entry_dir/prepare_image_sources.py" "$sdk"
sudo update-binfmts --enable qemu-aarch64
taskset -c "$cpuset" ./build.sh tspi_3m_rk3576_ubuntu_noble_server_defconfig
# 只调整本次生成的配置，用同一Ubuntu仓库的国内镜像下载软件包。
sed -i "s/^RK_UBUNTU_MIRROR=.*/RK_UBUNTU_MIRROR=\"$mirror\"/" output/.config
sed -i 's/^RK_KERNEL_CFG_FRAGMENTS=.*/RK_KERNEL_CFG_FRAGMENTS="rk3576_panfrost.config rk3576_server_console.config rk3576_image_rt.config"/' output/.config
printf 'UBUNTU_MIRROR=%s\n' "$mirror"
taskset -c "$cpuset" ./build.sh all
bash "$entry_dir/finish_image.sh"
sudo python3 "$entry_dir/verify_image_files.py" "$sdk" "$RK_SYSTEM_EVIDENCE_DIR/packed_image_file_verification.json"
cp output/.config "$entry_dir/configs/system.config"
cp kernel-6.1/.config "$entry_dir/configs/kernel.config"
test -s rockdev/update.img
cp --dereference --sparse=always rockdev/update.img "$output"
sha256sum "$output"
printf 'SYSTEM_IMAGE_BUILD_EXIT=0\n'
