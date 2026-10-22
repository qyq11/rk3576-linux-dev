set -euo pipefail
export LC_ALL=C
release=6.1.99-rt36-tspi
run=/home/rkshare/programs/rt_validation/rt_$release
test "$(uname -r)" = 6.1.99
test "$(readlink -f /dev/disk/by-partlabel/boot)" = /dev/mmcblk0p3
test ! -e "/lib/modules/$release"
cd "$run"
printf '%s  %s\n' beaeb5597048534ebbc8beabd887fea1d14403463510347a2aae189e2ca0a2a9 "modules-$release.tar.gz" | sha256sum -c -
{
  sudo -n find /lib/modules/6.1.99 /boot -type f -exec sha256sum {} +
  sha256sum /etc/samba/smb.conf /etc/fstab /etc/passwd
} | sort > old_files_before.sha256
sudo -n tar --no-same-owner -xzf "modules-$release.tar.gz" -C /
(cd / && sha256sum -c "$run/module_files.sha256") > module_verify.log
{
  sudo -n find /lib/modules/6.1.99 /boot -type f -exec sha256sum {} +
  sha256sum /etc/samba/smb.conf /etc/fstab /etc/passwd
} | sort > old_files_after.sha256
cmp old_files_before.sha256 old_files_after.sha256
modinfo -F vermagic "/lib/modules/$release/kernel/drivers/net/wireless/rockchip_wlan/rkwifi/bcmdhd/bcmdhd.ko"
test "$(find "/lib/modules/$release" -type f -name '*.ko' | wc -l)" = 155
printf 'MODULES_INSTALLED=%s\nMODULE_FILE_HASHES=PASS\nOLD_MODULES_BOOT_AND_SELECTED_CONFIGS=UNCHANGED\nCURRENT_KERNEL=' "$release"
uname -r
printf 'BOOT_DEVICE_SHA256='
sudo -n sha256sum /dev/mmcblk0p3
printf 'BOOT_NOT_WRITTEN=YES\n'
