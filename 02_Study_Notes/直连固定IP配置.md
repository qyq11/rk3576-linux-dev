# 泰山派直连固定 IP 配置

准备及验证日期：2026-10-21。状态：**固定 IP 已生效，本次重启后仍为 192.168.100.123/24，Windows ping 通过，SSH 22/TCP 可达；账号登录待验证**。证据见 [固定 IP 重启与 SSH 端口验证](../04_Test_Records/20261021_T02_固定IP重启与SSH端口验证.md)。下方创建命令保留作为操作记录，当前无需重复创建连接。

来源核对：立创[Ubuntu24 网口教程](https://wiki.lckfb.com/zh-hans/tspi-3-rk3576/system-usage/ubuntu24-usage/ethernet-internet.html)当前演示接路由器自动获取 IP，没有在该页给出直连静态地址命令。本页固定地址命令依据 NetworkManager 官方 `nmcli` 与连接属性文档，并按用户照片中已确认的 `end0` 和 NetworkManager 适配，不标作立创页面原文。

## 已确认的环境与地址

| 项目 | 值 |
| --- | --- |
| 连接方式 | Windows 与泰山派网线直连 |
| Windows 有线地址 | 已有 `192.168.100.200/24`，不需修改 |
| 板端有线接口 | `end0`，已显示 UP / LOWER_UP |
| 板端 MAC | `3e:c3:c5:4e:c4:22` |
| 板端管理工具 | NetworkManager；原连接 netplan-end0，当前重启后活动连接为 rk3576-direct |
| 用户指定板端地址 | **`192.168.100.123/24`** |
| 子网掩码 | `255.255.255.0` |
| 网段 | `192.168.100.0/24` |
| 直连网口默认网关 / DNS | 本阶段仅两端通信，不配置 |

`/24` 表示 IPv4 共 32 位中的前 24 位作为网络前缀，对应掩码 `255.255.255.0`。在该掩码下，两端前三段都是 `192.168.100`，最后一段 `.123` 与 `.200` 区分设备。斜杠写法把 IP 与前缀长度一起交给网络配置工具。

证据见[网线直连检查](../04_Test_Records/20261021_T02_网线直连检查.md)及[直连网口与网络管理状态](../04_Test_Records/20261021_T02_直连网口与网络管理状态.md)。

## 1. 在开发板本地创建直连配置

在 HDMI 显示器上的 `lckfb@localhost:~$` 后输入。此方案使用 NetworkManager 创建独立的持久连接 `rk3576-direct`，绑定 `end0`；原 `netplan-end0` 连接保留，可用于恢复。新连接自动连接优先级设为 100，后续仍需重启实测。

首次配置时，逐条执行以下命令。第一条创建配置，第二条补充自动连接与路由选项，第三条激活：

```bash
sudo nmcli connection add save yes type ethernet ifname end0 con-name rk3576-direct ipv4.method manual ipv4.addresses 192.168.100.123/24
sudo nmcli connection modify rk3576-direct connection.autoconnect yes connection.autoconnect-priority 100 ipv4.never-default yes ipv6.method link-local
sudo nmcli connection up rk3576-direct
```

每条命令均为单独一行，终端自动换行属于显示行为。`sudo` 提示密码时输入板卡本地登录密码，输入过程可能没有字符显示。

第一条用于首次创建。如果后续发现同名连接已存在，先用 `nmcli connection show rk3576-direct` 核实并修改已有配置，不重复添加同名连接。

`save yes` 明确将连接保存为持久配置，`ipv4.method manual` 使用手动地址；新连接没有配置网关和 DNS，`ipv4.never-default yes` 使此直连接口不承担 IPv4 默认路由；`ipv6.method link-local` 保留 IPv6 链路本地通信，不等待路由器提供 IPv6 配置。自动连接优先级 100 高于默认值 0，重启后的实际选择仍以复查结果为准。

## 2. 验证板端地址

```bash
ip -4 -br addr show end0
ip route
nmcli device status
```

预期看到：`end0` 包含 `192.168.100.123/24`，NetworkManager 使用 `rk3576-direct`，并有到 `192.168.100.0/24` 的直连路由。该接口没有默认路由符合本阶段规划。

可在板端尝试：

```bash
ping -c 4 192.168.100.200
```

若 Windows 不回复 ICMP，不能单独据此判定 IP 配置失败；继续在 Windows 发起测试并检查目标服务。

## 3. Windows 验证与 SSH

在 Windows PowerShell 执行：

```powershell
ping 192.168.100.123
Test-NetConnection -ComputerName 192.168.100.123 -Port 22
```

22/TCP 可达时，可发起登录：

```powershell
ssh lckfb@192.168.100.123
```

如果 IP 可达而 22/TCP 不通，再在板端检查 `systemctl status ssh --no-pager`、`ss -lnt` 等实际服务情况。不要仅因固定地址配置完成就认为 SSH 服务已配置成功。

## 4. 重启持久性与恢复

先确认板端已出现 `192.168.100.123/24`，然后在板端本地执行：

```bash
sudo reboot
```

重启并重新登录后执行：

```bash
ip -4 -br addr show end0
nmcli device status
```

若地址仍为 `192.168.100.123/24` 且活动连接为 `rk3576-direct`，则本次重启持久性验证通过；再从 Windows 执行 `ping 192.168.100.123`。固定 IP 的重启检查可以在 SSH 服务配置之前完成。T02 整体完成仍需额外验证 SSH 登录并保存证据。

如需恢复原连接，可在板端本地执行：

```bash
sudo nmcli connection modify rk3576-direct connection.autoconnect no
sudo nmcli connection up netplan-end0
```

这会让接口重新使用原连接配置；若原连接需要 DHCP，直连 Windows 时仍可能获取不到 IPv4。新直连配置仍保留，便于查阅和重新启用。

## 参考

- [NetworkManager nmcli 手册](https://networkmanager.pages.freedesktop.org/NetworkManager/NetworkManager/nmcli.html)：新增、修改和激活连接；普通新增配置保存为持久配置，运行效果仍需实测。
- [NetworkManager 连接属性](https://networkmanager.pages.freedesktop.org/NetworkManager/NetworkManager/nm-settings-nmcli.html)：手动地址、自动连接优先级、默认路由与 IPv6 链路本地方法。
