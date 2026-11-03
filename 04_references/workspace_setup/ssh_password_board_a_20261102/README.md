# 板A允许各电脑用root系统密码远程登录

2026-11-02，用户选择远程密码方式，希望其他电脑输入同一账号密码即可连接。板A身份通过硬件序列号`3bf8222fe1417f6a`及既有SSH主机指纹核实；板B未操作。

**已开启root远程密码登录。有线`192.168.100.123`和无线`192.168.3.38`均完成只用密码的SSH实际登录，root用户编号0、主目录`/root`及退出码0通过；原有密钥连接另建新连接核对通过。**无线地址是当时分配的地址，后续可能变化。

WindTerm新建SSH会话，填写板A对应IP、端口`22`、用户名`root`，选择密码认证并输入已设置的root系统密码即可。能够连到板A的电脑不需要逐台登记密钥；首次主机身份提示应核对板A主机指纹。共享密码由Samba单独管理，本轮没有重设任何密码。

此前生效值为`PermitRootLogin without-password`，只接受root密钥。现只新增`/etc/ssh/sshd_config.d/20-board-a-root-password.conf`，内容为`PermitRootLogin yes`；已有`PasswordAuthentication yes`、`PubkeyAuthentication yes`继续生效。先检查配置语法及有线、无线来源条件，再用`systemctl reload ssh`重新加载。没有更改原SSH主配置，也没有重启板卡。

测试在电脑WSL中运行SSH客户端，明确禁用密钥和键盘交互，只允许密码，并核对原板A主机指纹；密码从项目外本机加密凭据解密到内存，经客户端密码提示输入，不写入脚本、参数或日志。板端只执行身份、权限和目录核对命令，不运行Python。两条连接均为密码通过，板端日志另记录`Accepted password`，与之后的`Accepted publickey`分开。

操作进程内部比较结果表明：系统密码文件、root已有授权文件、Samba配置及SSH主配置保持原值；比较用密码摘要未输出或保存。没有操作Qt应用和串口/CAN配置。新电脑上的WindTerm界面仍由用户重连核对，本轮不把本机客户端测试写成已操作新电脑。

| 文件 | 用途 |
| --- | --- |
| `preflight.log` | 修改前板A身份、配置指纹及实际登录规则。 |
| `configure.sh`、`configure.log` | 新增配置的方法、服务重新加载及保留项核对。 |
| `check_ssh_password.py`、`check_ssh_password.ps1` | 电脑侧纯密码连接方法；脚本不含密码。 |
| `password_only_login.log` | 两个地址的密码提示、实际输出和退出码；附WSL自身的网络代理提示，不影响登录结果。 |
| `final_check.log` | 板端实际接受密码的记录及另一次密钥登录结果。 |
| `SHA256SUMS` | 上述7个文件的内容核对清单，README不纳入。 |

归档脚本用于查看本次方法；复测时复制到新的临时目录运行，避免覆盖固定日志。有限恢复输入见[本轮恢复目录](../../../07_backups/root_session/board_a_ssh_password_20261102/README.md)，同一事项记录见[工作环境记录](../../../02_records/20261021_workspace_setup.md#board-a-ssh-password-20261102)。配置含义见[OpenSSH官方说明](https://man.openbsd.org/sshd_config#PermitRootLogin)。
