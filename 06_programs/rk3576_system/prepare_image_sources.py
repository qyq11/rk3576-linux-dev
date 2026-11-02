#!/usr/bin/env python3
"""让每块新板在首次启动时生成自己的系统身份和SSH主机密钥。"""
from pathlib import Path
import hashlib
import shutil
import sys

sdk = Path(sys.argv[1]).resolve()
ubuntu = sdk / "ubuntu"
target = ubuntu / "chroot-script.sh"
marker = "# RK3576: prepare unique identity for each new board"
current = target.read_bytes()
anchor = b"\n\n# RK3576: prepare unique identity for each new board\n"
base = current.split(anchor, 1)[0] if anchor in current else current
expected_sha256 = "dd8c4c5c2267c2cd8cbf005591c79dbc0fa6ab47385d134822ec909f2ee3fd20"
if hashlib.sha256(base).hexdigest() != expected_sha256:
    raise SystemExit("Ubuntu构建脚本已有其他修改，请先核对；本脚本没有覆盖它。")
firstboot = (ubuntu / "config/hooks/post-install.sh").read_text()
if "ssh-keygen-firstboot.service" not in firstboot:
    raise SystemExit("没有找到官方首次启动生成SSH密钥的服务，停止修改。")
entry = Path(__file__).resolve().parent
hook = ubuntu / "config/hooks/server/zz-rk3576-environment.sh"
helper = ubuntu / "overlay/usr/local/sbin/rk3576-first-setup"
unit = ubuntu / "config/rk3576-ssh-keygen-firstboot.service"
post_hook = sdk / "device/rockchip/common/post-hooks/zz-rk3576-environment.sh"
hook.parent.mkdir(parents=True, exist_ok=True)
helper.parent.mkdir(parents=True, exist_ok=True)
shutil.copyfile(entry / "rootfs/setup-environment.sh", hook)
shutil.copyfile(entry / "rootfs/rk3576-first-setup", helper)
shutil.copyfile(entry / "rootfs/ssh-keygen-firstboot.service", unit)
shutil.copyfile(entry / "rootfs/post-rk3576-environment.sh", post_hook)
hook.chmod(0o755)
helper.chmod(0o755)
unit.chmod(0o644)
post_hook.chmod(0o755)
footer = r'''

# RK3576: prepare unique identity for each new board
# 原脚本已完成用户配置后，再统一主目录，避免旧绝对路径被重新创建。
test -d /home/rkshare
if [ "$(getent passwd lckfb | cut -d: -f6)" != /home/rkshare/lckfb ]; then
    test ! -e /home/rkshare/lckfb
    usermod -d /home/rkshare/lckfb -m lckfb
fi
chmod 0750 /home/rkshare/lckfb
test ! -e /home/lckfb
test -d /lib/modules/6.1.99-rt36-tspi
test -x /usr/local/sbin/rk3576-first-setup
testparm -s >/usr/share/rk3576-image/samba-config-check.txt
dpkg-query -W -f='${binary:Package}\t${Version}\n' > /usr/share/rk3576-image/installed-packages.tsv
# 清除编译期间产生的主机密钥和机器标识；新板首次启动时分别生成。
rm -f /etc/ssh/ssh_host_*
rm -f /var/lib/samba/private/passdb.tdb /var/lib/samba/private/secrets.tdb
: > /etc/machine-id
rm -f /var/lib/dbus/machine-id
ln -s /etc/machine-id /var/lib/dbus/machine-id
rm -f /var/lib/systemd/random-seed
echo 'New-board identity preparation complete'
'''
target.write_bytes(base + footer.encode())
print("IMAGE_ENVIRONMENT_PREPARATION=applied")
