# 板A共享改用root登录：实际验证

2026-11-02，板A硬件序列号`3bf8222fe1417f6a`，SSH ED25519主机指纹`SHA256:xLQv5sqzTRvKbfD2fNQQLYjCbPN6WOXgkXEoUqV8Yy4`。板B没有操作。

**结果：共享登录账号已改为root，用户指定的共享密码已设置；Windows实际登录及双向传文件、内容核对均通过。**地址仍为`\\192.168.100.123\rkshare`，板端目录仍为`/home/rkshare`。密码不写入工程、脚本或日志。

本次在Samba账号库新增root；配置只改`[rkshare]`中的`valid users = lckfb`为`valid users = root`。原lckfb共享账号记录保留用于恢复，但不再获准访问这个共享；Linux普通用户及其主目录保留。新增账号时明确关闭该次命令的系统密码同步，并在操作进程内比较前后内容，确认系统密码文件和原lckfb共享账号记录未变；比较用数据未输出或归档。

| 核对项 | 实际结果 |
| --- | --- |
| 板A身份、原配置和已有连接 | 身份一致；修改前没有活动共享连接。 |
| 生效配置及服务 | 配置检查通过，Samba服务为`active`，共享允许root访问和写入。 |
| Windows使用root和指定密码连接 | 成功，系统连接接口返回0；没有保存密码到Windows凭据库。 |
| Windows列出共享目录 | 成功，当时可见5个现有条目。 |
| Windows写文件、板A读取 | 文本`PC_TO_RK_SAMBA_ROOT_20261102`逐字核对通过。 |
| 板A写文件、Windows读取 | 文本`RK_TO_PC_SAMBA_ROOT_20261102`及换行核对通过。 |
| 清理 | 只删除本轮新建的两个测试文件及空测试目录，关闭本轮临时共享连接；最终板端核对无测试目录残留。 |
| 系统密码与原账号 | 系统密码文件和原lckfb共享账号记录与操作前一致。 |

测试使用独立随机目录，没有搬动或改写原共享文件。按原有创建权限，本轮从Windows新建的目录为`root:root 700`，文件为`root:root 600`；后续通过root共享新建文件也由root持有。未递归修改已有目录的所有者或权限，没有更改Qt应用、串口或CAN配置。

`preflight.log`是修改前状态；`configure.sh`和`configure.log`保存实际修改方法和输出；`verify_windows.ps1`及`windows_read_write.log`保存Windows实际连接和双向核对；`final_check.log`保存收尾状态。`SHA256SUMS`核对这些原件，README不纳入清单，便于补充说明。脚本需要从标准输入或调用参数接收密码，本身不含密码；其中配置脚本带修改前状态检查，不用于重复安装。

本次没有重启板卡，也没有执行回退；不新增Qt业务或仪器验证结论。有限恢复文件见[板A恢复输入](../../../../07_backups/samba/board_a_root_20261102/README.md)，操作继续记在[同一份Samba记录](../../../../02_records/20261021_Samba共享文件夹.md#board-a-root-share-20261102)。实现依据为[Samba账号管理说明](https://www.samba.org/samba/docs/current/man-html/pdbedit.8.html)及板端实际工具帮助。
