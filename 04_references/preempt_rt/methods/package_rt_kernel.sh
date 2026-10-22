set -euo pipefail
export LC_ALL=C
sdk=/home/qyq/rk3576/TaishanPi-3-Linux
work=/home/qyq/rk3576/preempt_rt_6.1.99_rt36_20261022
cross="$sdk/prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-"
test "$(cat "$work/build.exit")" = 0
release=$(cat "$work/kernelrelease.txt")
test "$release" = 6.1.99-rt36-tspi
test ! -e "$work/staging"
test ! -e "$work/artifacts"
mkdir "$work/artifacts"
make -C "$work/kernel" O="$work/build" ARCH=arm64 CROSS_COMPILE="$cross" \
    INSTALL_MOD_PATH="$work/staging" INSTALL_MOD_STRIP=1 modules_install > "$work/modules_install.log" 2>&1
depmod -b "$work/staging" -F "$work/build/System.map" "$release" > "$work/depmod.log" 2>&1
test ! -s "$work/depmod.log"
cd "$work/artifacts"
cp "$work/build/arch/arm64/boot/Image" Image
cp "$work/build/arch/arm64/boot/dts/rockchip/tspi-3m-rk3576-panfrost.dtb" kernel.dtb
cp "$work/build/.config" kernel.config
cp "$work/kernel/logo.bmp" logo.bmp
cp "$work/kernel/logo_kernel.bmp" logo_kernel.bmp
"$work/build/scripts/resource_tool" kernel.dtb logo.bmp logo_kernel.bmp > "$work/resource_pack.log" 2>&1
RK_SDK_DIR="$sdk" RK_SECURITY= bash "$sdk/device/rockchip/common/scripts/mk-fitimage.sh" \
    "$work/artifacts/boot.img" "$sdk/device/rockchip/.chips/rk3576/boot.its" \
    "$work/artifacts/Image" "$work/artifacts/kernel.dtb" "$work/artifacts/resource.img" "" > "$work/fit_pack.log" 2>&1
tar -C "$work/staging" --exclude="lib/modules/$release/build" --exclude="lib/modules/$release/source" \
    -czf "modules-$release.tar.gz" "lib/modules/$release"
"$work/kernel/scripts/extract-ikconfig" Image > image.config
cmp kernel.config image.config
cmp kernel.dtb "$sdk/kernel-6.1/arch/arm64/boot/dts/rockchip/tspi-3m-rk3576-panfrost.dtb"
size=$(stat -c %s boot.img)
test "$size" -le 67108864
printf 'BOOT_SIZE=%s\nRELEASE=%s\n' "$size" "$release"
printf 'MODULE_COUNT='
find "$work/staging/lib/modules/$release" -type f -name '*.ko' | wc -l
printf 'WIFI_MODULE\n'
modinfo "$work/staging/lib/modules/$release/kernel/drivers/net/wireless/rockchip_wlan/rkwifi/bcmdhd/bcmdhd.ko" | head -n 16
printf 'FIT_CONTENTS\n'
"$sdk/rkbin/tools/mkimage" -l boot.img
sha256sum Image kernel.dtb kernel.config resource.img boot.img "modules-$release.tar.gz" > SHA256SUMS
cat SHA256SUMS
printf 'PACKAGING=PASS\n'
