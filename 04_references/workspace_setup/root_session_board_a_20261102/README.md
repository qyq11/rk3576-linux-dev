# 板A开机及远程连接进入root

**后续已有调整：**用户随后要求开机输入root账户和密码，已取消本地自动登录，当前使用方式见[最新密码登录记录](../root_password_login_board_a_20261102/README.md)。本目录保留初次自动登录的证据和当时操作范围。

2026-11-02，用户要求屏幕接键盘和WindTerm连接两种方式都直接进入root。已确认板A序列号`3bf8222fe1417f6a`及原SSH主机指纹；板B没有操作。

**一次重启后，屏幕终端`tty1`和三针调试控制台`ttyFIQ0`实际登录账户均为root，真实Bash进程用户编号为0，当前目录及HOME均为`/root`。远程root密钥连接通过；WindTerm新增“板A-root”会话，按其保存的程序及参数分配交互终端实测，输出root、0和/root，实际退出0。**没有自动操作WindTerm的终端界面，界面双击及物理屏幕键盘人工操作另由用户核对。

## 用户如何使用

- 板A接屏幕和键盘，启动后直接进入root命令行；三针调试口沿用1500000速率，也自动登录root。
- WindTerm双击“lckfb-板A”（最初名称为“板A-root”，后按用户要求改名）。该会话调用Windows自带SSH，使用本电脑现有私钥和严格主机指纹核对，登录root后位于`/root`。无需在每条管理员命令前加`sudo`；命令会直接以管理员身份生效。
- `/`是整个文件系统的根目录，`/root`是root账户的主目录。当前需求是root身份和默认进入`/root`；可以用`cd /`进入系统根目录，也可以直接使用原来的绝对路径访问项目。

## 实际写入范围

新增两份系统服务补充配置：`/etc/systemd/system/getty@tty1.service.d/zz-board-a-root-autologin.conf`和`/etc/systemd/system/serial-getty@ttyFIQ0.service.d/zz-board-a-root-autologin.conf`。它们分别覆盖屏幕和调试口的登录命令；原串口`autologin.conf`逐字节保留，原来是自动登录lckfb。未更改登录服务模板或终端速率。

板A原`/root/.ssh/authorized_keys`不存在，本轮新增电脑已有的公钥，用于之后持续以root远程登录。root原主目录及SSH目录权限700，授权文件600；没有修改SSH服务设置、root密码、普通用户主目录或用户组，没有关闭主机身份核对。

WindTerm关闭后，仅在`C:\WindTerm\.wind\profiles\default.v10\terminal\user.sessions`增加“板A-root”Shell会话，调用`C:\Windows\System32\OpenSSH\ssh.exe`。原5个会话结构比较一致，包括原lckfb自动提权设置和认证字段，保存后共6个会话；随后重新打开WindTerm。恢复输入只保存新增会话，不保存含认证数据的整份用户配置。既有普通用户编译部署入口保留。

## 核对结果与材料

重启标识从`046347e9-a651-4392-af42-61fc37d00bcd`变为`c6a6264d-80d4-4593-b808-2f3fd4a308c9`。`who`和登录服务显示两个本地root会话；直接读取对应进程的用户编号、当前目录和HOME核对，没有另起提权进程代替本地登录结果。内核仍为`6.1.99-rt36-tspi #1`、实时标志1；五组串口入口、CAN0及两个启动设置指纹保留，未新增外设通信验收。

`preflight.log`、`serial_before.log`保存原状态；`configure.log`保存实际应用及文件指纹；`verify_after_reboot.log`保存重启后的真实本地登录及远程账户结果；`windterm_saved_arguments_test.log`保存与新增会话相同参数的交互连接输出；`windterm_change.json`仅记录会话新增与原会话不变的核对结果。`reboot_ready.log`保留重启期间一次SSH超时和随后恢复，没有删除该超时。`summary.json`登记已验范围和未做的界面人工确认。

除本说明外的文件用`SHA256SUMS`核对。执行脚本不含密码或密钥内容；认证凭据没有复制到项目。恢复方法见[板A有限恢复输入](../../../07_backups/root_session/board_a_20261102/README.md)，统一过程继续追加[原工作环境记录](../../../02_records/20261021_workspace_setup.md#board-a-root-login-20261102)。

实现依据：[systemd登录服务模板](https://github.com/systemd/systemd/blob/main/units/getty%40.service.in)及板A实际安装的服务文件；[OpenSSH的root登录设置说明](https://man.openbsd.org/sshd_config)。本轮沿用板A已有的root公钥登录能力，没有开放root密码远程登录。
