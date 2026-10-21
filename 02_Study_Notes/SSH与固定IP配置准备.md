# SSH 与固定 IP 配置准备

更新日期：2026-10-21。目标板卡：**立创泰山派 3M RK3576**。当前已完成官方资料核对与 Windows 网络基线采集；用户已通过 HDMI 终端本地登录，尚未执行板端网络配置或远程连接测试。

当前实机入口见 [HDMI 终端登录记录](../04_Test_Records/20261021_T01_HDMI终端登录.md)：欢迎信息显示 Ubuntu 24.04 Server，提示符为 `lckfb@localhost:~$`。用户已完成系统与网络初查，见 [系统与网络参数记录](../04_Test_Records/20261021_T02_系统与网络参数.md)：系统文件声明 Ubuntu 24.04.5 LTS / noble，运行 Linux 6.1.99 / aarch64；`end0` DOWN 且无地址，IPv4 主路由表为空。

用户现已确认 Windows 与开发板网线直连；Windows 的“以太网”实测 Up / 1 Gbps，现有地址 `192.168.100.200/24`。用户指定板端固定地址 **`192.168.100.123/24`**，尚未配置；之前的 `.201` 规划未执行，已被取代。

最新照片已确认 `end0` 为 UP / LOWER_UP，MAC 为 `3e:c3:c5:4e:c4:22`，仅有 IPv6 链路本地地址；NetworkManager 正在为 `netplan-end0` 获取 IP 配置。详见 [板端链路记录](../04_Test_Records/20261021_T02_直连网口与网络管理状态.md)。当前按 [直连固定 IP 配置](直连固定IP配置.md)在本地终端创建持久连接，使用用户指定的 `.123`，再验证地址、SSH 和重启效果。

## 1. 第一步：进入板卡终端，确认正在运行的系统

如果还不知道如何供电和连接串口，先阅读 [首次上电与串口终端](首次上电与串口终端.md)，按顺序完成工具准备、Windows COM 识别、断电接线、终端设置和上电观察。系统未知时，先获得启动日志；Android 的串口不一定提供 Linux 式登录终端。

先使用调试串口，或已可用的显示屏与键盘取得板卡终端。立创提供 [Ubuntu24](https://wiki.lckfb.com/zh-hans/tspi-3-rk3576/system-usage/ubuntu24-usage/serial-debug.html)、[Debian12](https://wiki.lckfb.com/zh-hans/tspi-3-rk3576/system-usage/debian12-usage/serial-debug.html)、[Buildroot](https://wiki.lckfb.com/zh-hans/tspi-3-rk3576/system-usage/buildroot-usage/serial-debug.html) 的串口教程。

串口教程使用侧边三针调试接口和 1500000 波特率；按具体板卡资料核对接线与电平，选择电脑实际出现的 COM 号。其余串口设置对照官方图示，不套用另一款 RK3576 板卡的引脚或参数。

在**板卡 Linux 终端**执行以下只读检查，保存完整输出：

```bash
cat /etc/os-release
uname -a
ip addr
ip route
```

如果精简系统缺少某条命令，记录原始提示；`ifconfig`、`route -n` 可在工具存在时辅助查看。若当前还是 Android，先记录实际系统和已有固件情况，后续按匹配的 Linux 镜像另行准备。

第一步的完成标准：知道实际系统、板卡网卡名、当前 IP/掩码及路由，且保留一个可用的本地终端入口。

## 2. 还需要哪些信息

| 信息 | 当前已知 | 获取方式 |
| --- | --- | --- |
| 厂家与完整型号 | 立创泰山派 3M RK3576，用户已确认 | 已记录 |
| 硬件版本与批次 | 待确认 | 板上丝印、标签或采购资料 |
| 当前系统、内核与固件来源 | 系统声明 Ubuntu 24.04.5 LTS / noble，运行 Linux 6.1.99 / aarch64；欢迎信息标识 Server，固件来源未知 | 已有参数照片；已有镜像文件名或烧录记录待补 |
| 是否能进入串口或本地终端 | 已通过 HDMI 显示器本地登录；串口待验证 | 用户说明及 HDMI 登录照片 |
| 网络连接方案 | 用户已确认 Windows 网线直连；Windows 网口 Up / 1 Gbps | 用户说明及 Windows 当前查询 |
| 板卡网卡名、MAC、IP、掩码、路由 | `end0` UP / LOWER_UP，MAC 3e:c3:c5:4e:c4:22；仅有 fe80::3cc3:c5ff:fe4e:c422/64；用户指定 IPv4 为 192.168.100.123/24、待设置 | 最新 `ip link`、`ip -br addr` 照片 |
| 网络由哪个工具管理 | 已确认 NetworkManager 管理 end0，当前连接名 netplan-end0 | 最新 `nmcli device status` 照片 |
| 登录用户名与管理权限 | 本地用户名 `lckfb`；管理权限待验证 | 当前终端登录身份；密码在板卡本地输入，无需提供给文档 |
| 是否只需本地 SSH，还是也需要板卡上网 | 待确认 | 确定是否需要默认网关、DNS 或网络共享 |
| 最终固定 IP 是否已有指定要求 | 待确认 | 同网段地址规划、路由器 DHCP 范围或网络管理员分配 |

## 3. Windows 端已采集到的信息

详见 [20261021 Windows 网络基线](../04_Test_Records/20261021_T02_Windows网络基线.md)。本机已有 OpenSSH 客户端，可以从 PowerShell 发起 SSH。

| 接口 | 观察结果 | 对本次开发的影响 |
| --- | --- | --- |
| WLAN | `192.168.3.37/24`，默认网关 `192.168.3.1`，链路 Up | 若板卡接该局域网且允许设备互访，可按实际分配地址登录 |
| 以太网 | 原基线 Disconnected；最新直连检查已为 Up / 1 Gbps，地址仍为 `192.168.100.200/24` | 已确认直连，沿用现有电脑地址规划板端；IP 互通及 SSH 待验证 |
| VMware VMnet1 / VMnet8 | `192.168.142.1/24`、`192.168.88.1/24` | 记录用于避免地址规划混淆，后续按实际用途选择接口 |

这些值来自本次执行所在 Windows 主机的快照，换电脑或换网络后需要重新采集。

## 4. 先确定连接方式与地址

| 方案 | 初次连接方法 | 固定地址规划 |
| --- | --- | --- |
| 经路由器 / 有 DHCP 的局域网 | 板卡接路由器 LAN，电脑在可互访的同一局域网；从串口或路由器查板卡地址 | 按实际网段和 DHCP 分配范围选择地址，或由路由器按 MAC 保留地址；是否要求板端静态配置需明确 |
| Windows 网线直连 | 确认两端链路接通，再在专用接口上设置同网段、不同地址 | 本机以太网已配置 `.100.200/24`；板卡地址需另定，不能与电脑相同 |

立创 [Ubuntu24 以太网教程](https://wiki.lckfb.com/zh-hans/tspi-3-rk3576/system-usage/ubuntu24-usage/ethernet-internet.html)演示的是路由器自动分配地址；单纯网线直连时，需要确认是否存在 DHCP 服务。

只在两端直接通信时可不配置默认网关和 DNS；需要跨网段或上网时，填写实际可用的路由与 DNS。地址是否可用应结合 DHCP 租约、地址保留或管理员分配核实，不能仅凭一次 ping 无响应判定空闲。SSH 的前提是网络可达，互联网连接不是本地 SSH 的必要条件。

## 5. 核实 SSH 服务端

在板卡终端检查已有程序和监听情况，缺少命令时保留提示：

```bash
command -v sshd
command -v dropbear
ss -lnt
```

程序未在 PATH 中找到不等于一定没有安装，结合监听、启动服务和镜像说明确认。若使用 systemd，可进一步查看 `systemctl status ssh`、`systemctl status sshd` 或实际存在的 socket/service；服务名及启动方式以实机为准。

- **Ubuntu**：参考 [Canonical OpenSSH 文档](https://ubuntu.com/server/docs/how-to/security/openssh-server/)。确认未安装服务端且软件源可达后，才使用 `sudo apt install openssh-server`，并核实启动及监听结果。
- **Debian**：按实际镜像的软件包和服务管理方式确认，记录现有配置。
- **Buildroot**：参考立创 [通信通道](https://wiki.lckfb.com/zh-hans/tspi-3-rk3576/system-development-compilation/qt-5-15/buildroot/communication-channels.html)。其示例镜像使用 root，文档要求先在本地终端执行 `passwd` 设置密码，再建立 SSH。是否与当前镜像一致仍需检查。

普通用户账号与认证方式以当前镜像为准。本次不会据其他厂商的默认账号修改板卡；无需为 Windows 主动登录板卡而启用 Windows SSH 服务端。

## 6. 固定 IP 的持久化配置分支

先在板卡上检查管理工具：

```bash
command -v nmcli
command -v nmtui
ls /etc/netplan
```

若有 `nmcli`，继续读取：

```bash
nmcli device status
nmcli -f NAME,UUID,TYPE,DEVICE connection show
```

命令或目录存在只是线索，还需核对目标接口由谁实际管理。目标接口显示 unmanaged 时，先查其现有配置来源。

| 实际管理方式 | 下一步 |
| --- | --- |
| NetworkManager 管理目标网卡 | 使用下面的条件模板，或以 `nmtui` 编辑实际连接配置 |
| Netplan | 依据现有 YAML、renderer 和网卡名编写配置，参考 [Netplan 官方静态地址示例](https://netplan.readthedocs.io/en/stable/examples/#how-to-configure-a-static-ip-address-on-an-interface)；取得原配置后再形成具体文件 |
| networkd、ifupdown 或 Buildroot 自定义脚本 | 先读取当前配置文件与启动脚本，再按对应版本准备修改方案 |

以下模板仅在**已确认 NetworkManager 管理目标接口、确定地址且保存原配置**后使用。中文值都是待替换参数，尚未在板卡执行。先记录 `nmcli connection show` 中该连接的原值和实际配置位置，并保留本地终端，便于地址切换后重新登录或恢复。

```bash
# 替换为实机查得的连接名、已确认的板卡地址与前缀长度
CONN='实际连接名'
ADDR='实际板卡IPv4/前缀长度'

sudo nmcli connection modify "$CONN" \
  ipv4.addresses "$ADDR" ipv4.method manual connection.autoconnect yes
```

然后根据已选拓扑**二选一**：

```bash
# A：需要通过实际网关访问其他网络时
sudo nmcli connection modify "$CONN" \
  ipv4.gateway '实际网关' ipv4.dns '实际DNS' ipv4.never-default no
```

```bash
# B：仅用于直连 SSH 的专用接口，不承担默认路由时
sudo nmcli connection modify "$CONN" \
  ipv4.gateway "" ipv4.dns "" ipv4.never-default yes
```

完成后在本地终端应用并检查：

```bash
sudo nmcli connection up "$CONN"
ip addr
ip route
```

上述属性、持久配置和激活行为依据 [NetworkManager nmcli 手册](https://networkmanager.pages.freedesktop.org/NetworkManager/NetworkManager/nmcli.html)及[连接属性手册](https://networkmanager.pages.freedesktop.org/NetworkManager/NetworkManager/nm-settings-nmcli.html)。恢复时按记录还原原连接参数并重新激活；原来是静态配置时，不能简单改回 DHCP 代替恢复。

仅临时设置网卡地址不足以满足“重启后仍固定”的要求，最终必须验证启动时实际加载的配置。

## 7. 从 Windows 验证与登录

在 **Windows PowerShell** 中，将以下参数换为板卡的实际值：

```powershell
$BoardIp = '实际板卡IP'
$BoardUser = '实际用户名'
$SshPort = 22  # 若板端使用其他监听端口，替换为实际端口

ping $BoardIp
Test-NetConnection -ComputerName $BoardIp -Port $SshPort
ssh -p $SshPort "${BoardUser}@${BoardIp}"
```

客户端用法参考 [Microsoft OpenSSH 文档](https://learn.microsoft.com/zh-cn/windows-server/administration/openssh/openssh_install_firstuse)，端口检查参考 [Test-NetConnection 文档](https://learn.microsoft.com/en-us/powershell/module/nettcpip/test-netconnection?view=windowsserver2025-ps)。首次连接时核对目标板卡与主机密钥提示；账号密码在终端输入，文档只记认证方式。

## 8. T02 验收与证据

- Windows 到板卡 SSH 端口可达，能登录并执行 `uname -a` 等命令。
- 已保存实际地址规划、网络管理方式、配置位置及修改前后差异。
- 已确认 SSH 的启动机制；板卡重启后，固定 IP、监听端口与登录仍可用。
- 接口配置、启动日志、端口测试和登录结果归档到 `04_Test_Records/`；确认后才将 T02 标为“已完成”。

当前下一步：已确认两端物理链路及 NetworkManager，执行 [192.168.100.123/24 直连固定 IP 配置](直连固定IP配置.md)，取得配置、IP 互通、SSH 登录与重启后的实测结果。
