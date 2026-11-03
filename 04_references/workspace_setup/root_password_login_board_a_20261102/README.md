# 板A改为开机输入root账户和密码

2026-11-02，用户在此前自动登录设置之后，明确要求开机输入密码再进入root，并指定root密码由本轮设置。**已设置指定密码，取消屏幕和三针调试口的自动登录；一次重启后两个终端都停在登录提示，实际密码提示登录root、用户编号0和`/root`核对通过。**

对象为板A，硬件序列号`3bf8222fe1417f6a`，SSH主机指纹与原板A一致；板B没有操作。本次只改本地登录方式及root密码，WindTerm“lckfb-板A”继续使用现有密钥连接。

## 现在怎样使用

屏幕接键盘，或连接1500000速率的三针调试口，开机后看到`localhost login:`。输入`root`并回车；看到`Password:`后输入已设置的密码并回车，输入过程中不会显示字符。验证通过后进入root命令行，默认目录`/root`，管理员命令不用加`sudo`。

用户名需要在登录提示处输入，本轮没有强制填入用户名。WindTerm沿用密钥验证，仍可直接连接root；本轮没有开放SSH的root密码认证。

## 实际怎样修改

先确认板卡身份和原服务状态，保存将被替换的两份`zz-board-a-root-autologin.conf`。通过加密SSH连接的标准输入调用系统`chpasswd`设置root密码，密码不放进命令参数、脚本或项目文件；明文及密码摘要均未归档。具体值仍只在项目外的本机加密凭据中保留。

新增以下两份服务补充配置，并移除此前的两份root自动登录补充配置：

| 终端 | 新配置位置 | 生效的登录命令 |
| --- | --- | --- |
| 屏幕终端`tty1` | `/etc/systemd/system/getty@tty1.service.d/zz-board-a-password-login.conf` | `/sbin/agetty --noclear - $TERM` |
| 三针口`ttyFIQ0` | `/etc/systemd/system/serial-getty@ttyFIQ0.service.d/zz-board-a-password-login.conf` | `/sbin/agetty --noclear %I 1500000 linux` |

`agetty`负责显示登录提示，随后由系统登录程序核对账户密码。新命令没有`--autologin`，所以不再跳过密码。三针口原`autologin.conf`保留原样，由后加载的新文件覆盖其启动命令，避免退回lckfb自动登录。没有更改厂商服务模板、root密钥、SSH服务设置、WindTerm配置、普通用户目录或Qt应用。

写入后执行`systemctl daemon-reload`重新读取服务配置，检查实际生效命令都不含自动登录参数，再同步并重启。受影响原配置、生效后文件及恢复方法见[本轮有限恢复输入](../../../07_backups/root_session/board_a_password_login_20261102/README.md)。

## 实际核对及首轮失败

- 设置密码的首轮系统命令退出0，但实际密码登录检查失败。原因是电脑输入流用了Windows换行；修正为Linux换行后重新设置，再观察到真实`Password:`提示，输入本机加密凭据中的密码，进入root后实际输出用户编号0及`/root`。首轮失败保留在`password_login_attempt01_failed.log`，没有登记为通过。
- 重启编号从`3bdb939a-ec87-4ed2-a3fe-029ec5fa19a9`变为`3df94f31-f1dc-43b7-a6c3-e2facf2895f1`。两个本地终端都只有等待输入的`agetty`，没有已登录Bash，`who`没有本地登录会话；屏幕缓冲区实际读到`localhost login:`。
- 重启后另在独立的交互终端运行系统`/bin/login root`，先出现密码提示，再输入密码，实际进入root、用户编号0、目录`/root`，退出0。这是系统登录程序的密码验证，没有把外层SSH密钥认证算成密码验证，也没有声称在实体键盘上人工输入通过。
- 内核仍为`6.1.99-rt36-tspi #1`、实时标志1，五组串口入口、CAN0及两个接口启动文件指纹保留。本轮没有发送外设数据或新增仪器验收。
- 重启等待期间的连接中断、一次SSH超时及随后恢复保留在`reboot_ready.log`。回退没有实际执行。

`preflight.log`保存修改前状态，`configure.log`保存写入及生效命令，`after_reboot_verification.log`和`screen_login_prompt.log`保存真实开机状态，`password_login_check.log`保存重启后的密码验证结果。登录输出经过密码脱敏，脚本从项目外密文读取凭据，项目中不含密码值。其余文件见`summary.json`和`SHA256SUMS`，可在本目录WSL终端运行`sha256sum -c SHA256SUMS`核对。

同一事项继续追加[工作环境记录第8.1.3节](../../../02_records/20261021_workspace_setup.md#board-a-password-login-20261102)。此前自动登录日志保留原范围，当前使用方式以本目录为准。
