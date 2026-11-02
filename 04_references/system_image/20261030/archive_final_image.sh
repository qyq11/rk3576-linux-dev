#!/usr/bin/env bash
set -euo pipefail
project=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
sdk=/home/qyq/TaishanPi-3-Linux
dest=$project/07_backups/system_image/ubuntu24_server
evidence=$project/04_references/system_image/20261030
test "${WSL_DISTRO_NAME:-}" = RK3576-Build-22.04
test "$(id -un)" = qyq
test ! -e "$dest"
grep -q '"failed": 0' "$evidence/rootfs_content_verification.json"
grep -q '"failed": 0' "$evidence/packed_image_file_verification.json"
test -s "$sdk/rockdev/update.img"
mkdir -p "$dest"
scratch=$(mktemp -d /tmp/rk3576-fit-check-XXXXXX)
dumpimage -l "$sdk/rockdev/boot.img" > "$evidence/boot_fit_information.txt"
dumpimage -T flat_dt -p 0 -o "$scratch/board.dtb" "$sdk/rockdev/boot.img"
dumpimage -T flat_dt -p 1 -o "$scratch/Image" "$sdk/rockdev/boot.img"
dumpimage -T flat_dt -p 2 -o "$scratch/resource.img" "$sdk/rockdev/boot.img"
cmp "$scratch/Image" "$sdk/kernel-6.1/arch/arm64/boot/Image"
cmp "$scratch/board.dtb" "$sdk/kernel-6.1/arch/arm64/boot/dts/rockchip/tspi-3m-rk3576-panfrost.dtb"
cmp "$scratch/resource.img" "$sdk/kernel-6.1/resource.img"
echo 'BOOT_FIT_COMPONENT_COMPARISON=passed'
for file in boot.img uboot.img MiniLoaderAll.bin parameter.txt; do
    cp --dereference "$sdk/rockdev/$file" "$dest/$file"
done
cp "$scratch/Image" "$dest/Image"
cp "$scratch/board.dtb" "$dest/tspi-3m-rk3576-panfrost.dtb"
cp "$sdk/output/update/Image/package-file" "$dest/package-file"
cp "$sdk/output/.config" "$project/06_programs/rk3576_system/configs/system.config"
cp "$sdk/kernel-6.1/.config" "$project/06_programs/rk3576_system/configs/kernel.config"
cp "$sdk/output/.config" "$dest/system.config"
cp "$sdk/kernel-6.1/.config" "$dest/kernel.config"
cp "$project/07_backups/system_image/sources/manifest_pinned.xml" "$dest/manifest_pinned.xml"
cp "$sdk/ubuntu/binary/usr/share/rk3576-image/environment.txt" "$dest/environment.txt"
cp "$sdk/ubuntu/binary/usr/share/rk3576-image/installed-packages.tsv" "$dest/installed-packages.tsv"
sudo tar -C "$sdk/ubuntu/binary/usr/lib" -czf "$dest/modules-6.1.99-rt36-tspi.tar.gz" modules
git -C "$sdk/ubuntu" diff -- chroot-script.sh > "$project/06_programs/rk3576_system/patches/ubuntu_identity.patch"
"$sdk/kernel-6.1/scripts/diffconfig" "$project/07_backups/preempt_rt/images/6.1.99-rt36-tspi/kernel.config" "$sdk/kernel-6.1/.config" > "$evidence/rt_config_comparison.txt"
test ! -s "$evidence/rt_config_comparison.txt"
python3 - "$sdk" "$evidence/compiled_image_source.json" <<'PY'
from pathlib import Path
import hashlib,json,sys
sdk=Path(sys.argv[1]); records={}
for name, path in [('update_img',sdk/'rockdev/update.img'),('rootfs',sdk/'ubuntu/ubuntu-rootfs.img')]:
    digest=hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda:stream.read(8*1024*1024), b''): digest.update(block)
    records[name]={'bytes':path.stat().st_size,'sha256':digest.hexdigest()}
with (sdk/'rockdev/update.img').open('rb') as stream: records['update_header']=stream.read(4).decode('ascii')
assert records['update_header']=='RKFW'
Path(sys.argv[2]).write_text(json.dumps(records,indent=2)+'\n')
PY
pigz -p 8 -6 -n -c "$sdk/rockdev/update.img" > "$dest/update.img.gz.partial"
pigz -t "$dest/update.img.gz.partial"
test "$(stat -c %s "$dest/update.img.gz.partial")" -lt 2147483648
mv "$dest/update.img.gz.partial" "$dest/update.img.gz"
echo 'IMAGE_ARCHIVE_INPUTS_PREPARED=0'
