#!/usr/bin/env bash
set -euo pipefail

# 本脚本只由系统镜像构建过程在新的根文件系统中调用。
test "$(id -u)" = 0
test -f /tmp/chroot-script.sh
export DEBIAN_FRONTEND=noninteractive
apt-get -o Dpkg::Use-Pty=0 install -y --no-install-recommends \
    samba openssh-server network-manager chrony rt-tests stress-ng \
    iproute2 iputils-ping ethtool lsof procps gpiod

getent passwd lckfb >/dev/null
install -d -o lckfb -g lckfb -m 0700 /home/rkshare
install -d -o lckfb -g lckfb -m 0750 /home/rkshare/programs

if ! grep -q '^\[rkshare\]' /etc/samba/smb.conf; then
    cat >> /etc/samba/smb.conf <<'EOF'

[rkshare]
    path = /home/rkshare
    browseable = yes
    read only = no
    guest ok = no
    valid users = lckfb
    create mask = 0600
    directory mask = 0700
EOF
fi
testparm -s >/dev/null

install -d -m 0700 /etc/NetworkManager/system-connections
cat > /etc/NetworkManager/system-connections/rk3576-direct.nmconnection <<'EOF'
[connection]
id=rk3576-direct
uuid=702e01e4-f710-52a3-a1b4-4e760868c376
type=ethernet
interface-name=end0
autoconnect=true
autoconnect-priority=100

[ethernet]

[ipv4]
method=manual
address1=192.168.100.123/24
never-default=true

[ipv6]
method=link-local
EOF
chmod 0600 /etc/NetworkManager/system-connections/rk3576-direct.nmconnection
install -d /etc/netplan
cat > /etc/netplan/90-rk3576-image.yaml <<'EOF'
network:
  version: 2
  renderer: NetworkManager
EOF
chmod 0600 /etc/netplan/90-rk3576-image.yaml
# 直接生成缺失的主机密钥，避免首次启动时重配置软件包并再次操作SSH服务。
install -m 0644 /config/rk3576-ssh-keygen-firstboot.service /etc/systemd/system/ssh-keygen-firstboot.service
SYSTEMD_OFFLINE=1 systemctl enable ssh-keygen-firstboot.service ssh.service smbd.service nmbd.service NetworkManager.service chrony.service

install -d /usr/share/rk3576-image
cat > /usr/share/rk3576-image/environment.txt <<'EOF'
RK3576 Ubuntu 24.04 Server with PREEMPT_RT 6.1.99-rt36-tspi
有线网口end0：192.168.100.123/24，不设置默认网关。
共享目录：/home/rkshare；用户主目录：/home/rkshare/lckfb。
SSH、Samba、NetworkManager和chrony已设置为开机启动。
首次共享登录前执行sudo rk3576-first-setup，现场设置Samba密码。
未加入SN100程序、旧板数据、私人SSH密钥或旧Samba账号库。
编译及镜像内容核对不能代替新板启动和网络共享实测。
EOF
printf 'RK3576_ENVIRONMENT_SETUP_EXIT=0\n'
