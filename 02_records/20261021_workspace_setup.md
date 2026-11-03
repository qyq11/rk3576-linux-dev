# 程序目录与 WindTerm WSL 工作目录

**范围：电脑工作环境；涉及RK的历史操作为板 A。**

日期：2026-10-21。环境：Windows、`C:\WindTerm\WindTerm.exe`、WSL 2 Ubuntu-24.04，Ubuntu 用户 `qyq`。

当前有效的Windows普通权限启动配置见第6节；原VM的免密连接会话见第7节；原lckfb会话自动进入root见第8节；板A开机和“lckfb-板A”直连设置见第8.1节。前面的目录设置与升级步骤保留为当时的操作记录。

## 1. 程序存放规则

项目根目录为 `C:\Users\52400\Desktop\internship\seerain\20260907_RK3576_Linux`，WSL 对应 `/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux`。Windows 编辑器和 WSL 访问同一份文件。

| 内容 | 位置 |
| --- | --- |
| 后续正式程序源码、头文件和构建文件 | `06_programs/<program>/` |
| GPIO、UART 等独立接口测试源码 | `05_tools/tests/<interface>/`，已有文件保留原位 |
| 编译产物、缓存、临时日志和一次性脚本 | 根目录 `temp/<program>/` |
| 验证记录、图片 | `02_records/`、`03_photos/` |

已建立 `06_programs/` 并加入实际生效的 `.gitignore`，排除误放入的本地编译产物；正式源码和构建文件可以纳入 Git。具体程序开始编写时再建立子目录。此前 GPIO 验证使用的 WSL `~/rk3576-linux-dev` 保留为历史构建目录，原 GPIO 记录中的复现路径不改写。

## 2. WindTerm 配置

用户正常退出 WindTerm 后，备份原配置，再修改现有的 `wsl` Shell 会话。配置文件为 `C:\WindTerm\.wind\profiles\default.v10\terminal\user.sessions`，只修改该会话的两个字段，其余会话和设置经结构比较确认一致。

| 字段 | 原值 | 新值 |
| --- | --- | --- |
| `process.arguments` | `C:\Windows\System32\wsl.exe -d Ubuntu-24.04 -u qyq --cd /home/qyq` | 见下方完整启动配置 |
| `process.workingDirectory` | `$(HomeDir)` | `C:\Users\52400\Desktop\internship\seerain\20260907_RK3576_Linux` |

对应 WindTerm **会话设置 → Shell → 进程 → 程序 [参数]** 的完整配置如下，此处是配置内容：

```text
C:\Windows\System32\wsl.exe -d Ubuntu-24.04 -u qyq --cd /mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
```

原配置曾备份在本机 `C:\Users\52400\.rk3576-local\windterm\user.sessions.20261021_183914.bak`，未放入项目或 Git；随后已按用户要求永久删除，见第 5 节。

## 3. 验证结果

- WSL 当前用户实测为 `qyq`；`wslpath -w "$PWD"` 将项目路径转换为指定的 Windows 根目录。
- Windows 与 WSL 读取同一份 README 的 SHA-256 一致。
- 从已保存的 WindTerm 会话读取启动参数和 Windows 工作目录，用该配置启动 WSL，再运行交互式登录 Bash 的 `pwd`；退出状态为 0，实际输出如下。
- `06_programs/` 在 Windows 与 WSL 中均存在；Git 忽略检查确认误放入 `build/` 的目标文件被排除，`.c` 源码与 `CMakeLists.txt` 不被排除。

实际工作目录输出：

```text
/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
```

以上验证覆盖配置文件与 WSL 登录 shell 的启动目录，未采集 WindTerm 界面截图。重新打开 WindTerm，双击原有 `wsl` 会话即可使用该配置。需要现场核对时，在新会话执行：

```bash
pwd
```

预期与上述路径一致。此次设置针对打开 `wsl` 会话后的目录；程序开发入口与分类已同步到根 README。

配置格式参考：[WindTerm 作者说明会话保存在 JSON 格式的 user.sessions 中](https://github.com/kingToolbox/WindTerm/discussions/69)。

## 4. WindTerm 升级到 2.7.0

2026-10-21，按用户要求将 `C:\WindTerm` 中的 WindTerm 从 2.5.0 升级为官方 Windows x64 便携版 2.7.0，保留现有会话和本项目的 WSL 默认目录。

- 安装包来自 [WindTerm 2.7.0 官方发布页](https://github.com/kingToolbox/WindTerm/releases/tag/2.7.0)，本地归档在 `05_tools/terminal/windterm/WindTerm_2.7.0_Windows_Portable_x86_64.zip`，仅本地保存，已确认被 Git 忽略。
- 安装包 SHA-256 为 `b05c3c7371340ff1302967247ffcd3a3ba27b237f72010e9157086143c453e50`，与官方公布的 Windows x64 校验值一致。
- 退出状态下完整备份旧版的 137 个文件，并逐一核对 SHA-256。当时的完整备份位于 `C:\Users\52400\.rk3576-local\windterm\backup_2.5.0_20261021_184309`，另包含替换时移出的旧程序文件；此备份随后已按用户要求永久删除，见第 5 节。
- 保持原 `.wind` 目录与 `profiles.config` 不动，将旧程序文件移至备份，再放入官方 2.7.0 的 4641 个程序文件；文件数量、大小及主程序 SHA-256 核对通过。原 9 个配置文件在替换前后逐一校验一致。
- 主程序内嵌版本确认为 2.7.0，SHA-256 为 `7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0`。已从原路径启动新版，进程正常进入就绪状态，主窗口标题为 `WindTerm`。
- 新版启动后，原有 `rock100`、`lckfb`、`wsl` 三个会话的 UUID、名称、连接参数及 WSL 启动配置均与当时备份一致。应用自行重新保存了 `lckfb` 的 `session.autoLogin` 字段，字段仍存在；没有覆盖应用保存的该字段。本次未额外验证 WindTerm SSH 自动登录。
- 再次读取保存的 `wsl` 会话启动参数并运行登录 Bash 的 `pwd`，退出状态为 0，仍返回 `/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux`。WSL 默认目录保持不变。

新版仍位于 `C:\WindTerm\WindTerm.exe`，原有指向该路径的快捷方式可继续使用。

## 5. 清理旧版与升级临时文件

2026-10-21，用户明确要求保留新版、删除旧版，并且不保留恢复副本。已永久删除 2.5.0 备份目录中的 265 个文件（含重复保存的旧程序，共 98,586,357 字节），并清除该项在回收站中的内容与元数据。此前的旧会话配置备份、升级过程的 `upgrade_state.json` 和临时说明也已永久删除。

保留 `C:\WindTerm` 的 2.7.0 程序、当前 `.wind` 配置，以及 `05_tools/terminal/windterm/` 中的官方 2.7.0 安装包。清理后重新核对新版主程序 SHA-256 和三个已有会话。核对时当前 `wsl` 会话的默认目录为项目下的 `06_programs`，清理未修改该设置。

## 6. 修复启动时的 gsudo 管理员授权弹窗

2026-10-21，用户反馈每次打开 WindTerm 都出现管理员授权窗口。[查看用户提供的原始照片](../03_photos/05_电脑开发环境与Qt调试/20261021_windterm_uac_01.jpg)，照片原文件已归档，字节校验与用户原件一致。弹窗显示程序为 `C:\WindTerm\vendors\gsudo\gsudo.exe`。

**原因：**`wsl` 和 `windows` 会话的 `session.target` 仍指向 gsudo；当时进程检查也确认是 `gsudo.exe` 在启动 WSL。修改默认目录时只调整参数，不会消除这个管理员启动目标。

用户正常退出 WindTerm 后，仅修改两个会话的启动目标及对应参数，共四个字段：

| 会话 | `session.target` | `process.arguments` |
| --- | --- | --- |
| `wsl` | `C:\Windows\System32\wsl.exe` | `-d Ubuntu-24.04 -u qyq --cd /mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs` |
| `windows` | `C:\Windows\System32\cmd.exe` | `/s /k "${AppDir}/vendors/clink/clink_x64.exe" inject` |

其余字段经结构比较确认未改变，包括 SSH 会话、自动登录字段及 WSL 的 Windows 工作目录 `C:\Users\52400\Desktop\internship\seerain\20260907_RK3576_Linux\06_programs`。本次未更改 Windows UAC 设置。

**实际验证：**从非管理员进程重新启动 WindTerm 2.7.0，程序正常就绪并恢复 WSL 会话，进程链为：

```text
WindTerm.exe → winpty-agent.exe → wsl.exe
```

实际 WSL 命令参数包含 `-d Ubuntu-24.04 -u qyq --cd /mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs`；WindTerm 后代进程中的 `gsudo.exe` 数量为 0。启动后复查两个 Shell 会话的保存目标，均为对应的系统程序。此项验证覆盖普通权限启动和 WSL 会话恢复，未自动操作 UAC 弹窗。

## 7. 新增原 VM 的免密连接会话（2026-10-22）

用户要求将原 SDK 所在的 VM 加入 WindTerm。由用户在单独的交互终端输入 VM 的 `qyq` 密码，使用 WSL 的 `ssh-copy-id` 安装已有公钥；公钥安装和 `BatchMode=yes` 免密复核均返回 0。密码未保存到项目。

WindTerm 正常退出后，在 `C:\WindTerm\.wind\profiles\default.v10\terminal\user.sessions` 新增会话，再从原路径重新启动程序：

| 配置 | 实际值 |
| --- | --- |
| 会话名称 | `rk3576-vm` |
| 会话 UUID | `b6676a3c-a77f-4d1d-9a20-e3883b8a687b` |
| 会话类型 | `Shell`，通过 WSL OpenSSH 连接原 VM |
| 启动程序 | `C:\Windows\System32\wsl.exe` |
| WSL 环境 | `Ubuntu-24.04`，用户 `qyq` |
| SSH 目标 | `qyq@192.168.88.128`，端口 22 |
| 认证 | 复用 WSL 已有 Ed25519 密钥，私钥继续保留在 WSL 用户的 `.ssh` 目录 |
| 登录后的目录 | `/home/qyq/rk3576/TaishanPi-3-Linux` |

`process.arguments` 保存内容：

```text
-d Ubuntu-24.04 -u qyq -- ssh -tt -i /home/qyq/.ssh/id_ed25519 -o IdentitiesOnly=yes -o BatchMode=yes -o StrictHostKeyChecking=yes -o ConnectTimeout=10 qyq@192.168.88.128 "cd /home/qyq/rk3576/TaishanPi-3-Linux && exec bash -l"
```

双击 **`rk3576-vm`** 启动连接。此会话使用 WSL SSH，文件传输可沿用 WSL 的 `scp`；WindTerm 原生 SSH / SFTP 会话不属于本次交付方式。直接调用 WindTerm 自带 SSH 库的认证检查未通过，因此采用已验证的 WSL 通道；未向项目复制私钥。

**实际验证：**使用与保存配置相同的进程参数启动交互式 SSH，登录后打印工作目录并退出，返回码为 0：

```text
VM_SDK_CWD=/home/qyq/rk3576/TaishanPi-3-Linux
```

独立免密查询返回用户 `qyq`、主机 `qyq-virtual-machine`、架构 `x86_64`。配置写入和程序重新启动后，会话数为 5，新增会话为 1；与本次备份逐项比较，原有 `rock100`、`lckfb`、`wsl`、`windows` 四个会话全部字段保持一致。未采集 WindTerm 新会话界面截图。

本次会话备份在项目外的 `C:\Users\52400\.rk3576-local\windterm\user.sessions.before_vm_8a562b4c6d.bak`。VM 的 IP 后续变化时需更新新会话目标。SDK 路径及源码核查继续见[实时内核统一记录](20261022_preempt_rt.md)。

## 8. RK 会话连接后自动进入 root（2026-10-22）

用户要求每次重新连接即可使用 root，无需手动输入 `sudo -i`。本次只为 WindTerm 原有 `lckfb` 会话增加连接后的自动命令，SSH 认证仍使用该会话原来的 lckfb 认证，终端登录后自动进入 root shell。[WindTerm 官方说明](https://github.com/kingToolbox/WindTerm/blob/master/README.md)确认支持 SSH 认证成功后的自动执行命令。

### 范围与实际设置

操作前确认 WindTerm 已退出，原始会话数为 5；RK 的 `sudo -n -i` 能无交互进入 root。未修改板端 SSH 配置、root 密码或密钥、sudo 授权、账号主目录和应用部署入口。

配置文件仍为 `C:\WindTerm\.wind\profiles\default.v10\terminal\user.sessions`。仅在 UUID 为 `e6362478-eaa7-415c-a20b-7a8e610b2f31`、名称为 `lckfb` 的原会话中增加：

| 字段 | 变更前 | 变更后 |
| --- | --- | --- |
| `session.autoExecution` | 未单独设置 | `sudo -n -i \r`，末尾为 WindTerm 的回车转义 |
| `session.autoExecutionDelay` | 未单独设置 | `1` 秒 |

原有 `rock100`、`wsl`、`windows`、`rk3576-vm` 四个会话，以及 `lckfb` 会话的其余全部字段，经结构比较确认未变，包含应用保存的认证字段。只插入以上两个配置字段，没有把凭据导出到项目。最小回退资料为 [`07_backups/root_session/20261022/`](../07_backups/root_session/20261022/README.md)：仅保存这两个字段的原存在状态、变更后值、恢复脚本和 SHA-256 清单；未复制含认证数据的整份 user.sessions。

### 实际验证与使用

读取保存后的配置，核对自动命令、延迟、原会话 UUID 和会话数 5，返回 `SAVED_CONFIGURATION_CHECK=PASS`。再通过 WSL 使用已有密钥建立交互式 SSH（分配 PTY），执行相同的 `sudo -n -i`，板端实际返回：

```text
AUTO_ROOT_USER=root
AUTO_ROOT_UID=0
AUTO_ROOT_HOME=/root
AUTO_ROOT_PWD=/root
AUTO_ROOT_CHECK=PASS
```

root shell 正常退出并关闭测试连接；[原始交互日志](../04_references/workspace_setup/root_session_20261022/interactive_20261022.log)保留终端控制字符，按回车分行后逐项核对上述结果。[配置与核查日志](../04_references/workspace_setup/root_session_20261022/)已正式归档，一次性辅助脚本已清理；回退仍使用前述 `07_backups/root_session/` 正式资料。本次验证了已保存的会话配置与相同命令的实际交互提权；没有启动 WindTerm 并采集该会话界面，因此首次 图形界面 连接结果以实际打开该会话为准。

以后打开 WindTerm，双击原来的 **`lckfb`** 会话，认证成功后约 1 秒自动进入 root，提示符应为 `root@localhost:~#`。此时终端命令以 root 执行，主目录为 `/root`；`exit` 可返回原 lckfb shell。SFTP 仍使用会话的 lckfb 身份；AI 构建部署工作流继续按项目约定使用普通用户。

`lckfb` 账号主目录仍为 `/home/rkshare/lckfb`，旧 `/home/lckfb` 链接未重建，详见[主目录统一记录](20261021_Samba共享文件夹.md#7-账号主目录统一到共享目录2026-10-22已完成)。Windows 上的 WindTerm 继续普通权限启动，不会因此触发 gsudo 或 Windows 管理员授权。

<a id="board-a-root-login-20261102"></a>
### 8.1 板A开机和WindTerm登录设置（2026-11-02，含后续调整）

**当前方式：本地开机输入root账户和密码，WindTerm既可用root系统密码，也保留原有密钥连接。**本地设置见[第8.1.3节](#board-a-password-login-20261102)，随后启用的远程密码登录见[第8.1.4节](#board-a-ssh-password-20261102)。下方初次自动登录和密码登记部分保留当时的操作经过。

本项发生在串口和CAN配置之后；同日从确认板卡到最终登录设置的先后顺序见[板A重新配置全过程](20261021_TTL串口调试验证.md#board-a-configuration-process-20261102)。下方保留root登录的具体改动及实际核对结果。

用户表示每次启动进入立创的lckfb用户目录、管理员操作需要sudo，要求屏幕接键盘及WindTerm都直接进入root。本轮只操作已核实的板A，硬件序列号`3bf8222fe1417f6a`，SSH ED25519指纹仍为`SHA256:xLQv5sqzTRvKbfD2fNQQLYjCbPN6WOXgkXEoUqV8Yy4`。

先核清区别：命令权限由账户决定，`/`是系统根目录，`/root`是root账户主目录；本次实现以root登录并默认位于`/root`。原lckfb账户和`/home/rkshare/lckfb`保留。操作前屏幕终端tty1等待登录，三针调试口ttyFIQ0已自动登录lckfb，root账户主目录原来就是/root。

用户在本机SSH窗口输入一次lckfb密码后，使用已有sudo授权建立设置通道；密码不显示、不保存。板A原SSH设置允许root密钥登录，原root授权文件不存在，本轮新增本电脑已有公钥，使root直连长期可用；未改SSH服务设置、root密码或普通用户权限，没有导出私钥或授权文件。

板端只新增两个`zz-board-a-root-autologin.conf`，分别位于`/etc/systemd/system/getty@tty1.service.d/`和`/etc/systemd/system/serial-getty@ttyFIQ0.service.d/`，令屏幕及三针调试口自动登录root。三针口仍为1500000，原有`autologin.conf`指纹保持`1b12ee238e4193a1c811ac5ceafba0627f44674e218b6fe0db7af0366529a203`，登录服务模板未改。

用户正常退出WindTerm后，在原`user.sessions`数组末尾新增“板A-root”Shell会话，目标为Windows已有的`C:\Windows\System32\OpenSSH\ssh.exe`，参数使用本电脑现有密钥、严格主机指纹核对、`root@192.168.100.123`；原5个会话包括认证字段均结构比较不变，保存后共6个。未备份整份含认证数据的配置，只归档新增会话和恢复所需的存在状态，随后重新打开WindTerm。

重启编号由`046347e9-a651-4392-af42-61fc37d00bcd`变为`c6a6264d-80d4-4593-b808-2f3fd4a308c9`。实际`who`与登录服务显示root在tty1、ttyFIQ0；读取这两个终端已自动生成的Bash进程，用户编号均为0、当前目录及HOME均为`/root`，核对脚本退出0。不是在检查时额外执行sudo代替开机登录。

再读取WindTerm新增会话保存的程序与参数，分配交互终端实际连接，输出`root`、`0`、`/root`，退出0。这个范围证明root连接及所存参数可用；没有自动操作WindTerm终端界面，界面双击和屏幕物理键盘仍由用户人工确认。日后WindTerm双击“lckfb-板A”，进入root命令行，管理员命令直接执行；原lckfb会话仍保留原来登录后自动提权方式。

重启后内核仍为`6.1.99-rt36-tspi #1`、实时标志1，五组串口及CAN0存在，原两个接口启动设置指纹一致。本轮没有重发串口/CAN数据，也没有启动或验收Qt业务。第一次重启等待中的SSH超时原文保留，随后连接恢复；没有将其认定为已确证故障。

[完整证据与使用方法](../04_references/workspace_setup/root_session_board_a_20261102/README.md) · [板A有限恢复输入](../07_backups/root_session/board_a_20261102/README.md)。两份新增本地配置可按说明停用；回退本轮没有实际执行，不把恢复输入齐全说成恢复功能验收通过。

#### 8.1.1 同日按用户要求改会话名称

用户要求把列表中的名称改为小写lckfb并带板A标记。本次将同一会话由“板A-root”改为“lckfb-板A”，UUID仍为`cd7e2631-5618-442e-bf9a-7c633914f761`。用户正常关闭WindTerm后，只替换该会话的`session.label`，逐项比较确认其他字段及其余会话均不变，共6个会话；保存后重新打开，名称回读一致。没有再次连接板卡或更改root登录、通信及应用配置。临时核对结果在`temp/root_session/board_a/20261102/rename_result.json`；恢复原显示名只需将该UUID的标签改回“板A-root”，原恢复目录中的JSON仍保留首次创建时名称，不改固定证据。

#### 8.1.2 用户自行设置root密码及自动登录说明（2026-11-02）

用户告知已自行设置板A的root密码，并要求记录。具体值只保存在本机项目外的加密凭据文件`C:\Users\52400\.rk3576-local\credentials\board_a\root.credential.clixml`，板卡身份和来源写在同目录`README.json`。凭据按Windows当前账户加密，目录仅当前账户和SYSTEM可访问；本机加密保存及解密回读核对通过。项目、Git及备份目录不保存密码明文、密码摘要或该凭据文件。本次依据用户告知登记，没有重新连接板A验证密码，也没有再次修改密码。

用户随后问为什么设置密码后仍直接进入root。原因是本轮先前已为屏幕和三针调试口设置root自动登录，登录程序通过`--autologin root`跳过输入密码；设置账户密码不会取消这两份自动登录配置。WindTerm“lckfb-板A”使用电脑密钥验证，同样不询问root密码。当前SSH的root登录策略仍只允许密钥，没有因用户设置密码而开放root密码远程登录。

因此当前行为仍是开机自动进入root，默认目录`/root`。如后续要求开机必须输入root密码，需要另行停用本轮两个自动登录补充配置，再核对本地登录；若还要求WindTerm改用root密码，也需单独核对并修改SSH登录策略。用户本次只要求记录和解释，没有要求取消自动登录或改变远程认证，本次未作这些变更。

<a id="board-a-password-login-20261102"></a>
#### 8.1.3 按最新要求取消自动登录，开机输入密码后进入root（2026-11-02）

用户进一步明确要求“开机之后需要输密码并进入root账户”，并要求由本轮设置其指定密码。操作前重新核对板A硬件序列号`3bf8222fe1417f6a`和原SSH主机指纹；板B未操作。root账户已有密码，但本轮仍按用户指定值完成设置，再实际验证，不把之前仅登记用户告知的状态当作本轮验证通过。

具体密码由本机项目外的加密凭据读取，经SSH标准输入送入板端`chpasswd`，不出现在命令参数、脚本或项目日志里。首轮系统工具退出0，但密码提示登录失败；检查发现输入流用了Windows换行，修正为Linux换行后重新设置，再实际观察到`Password:`提示并完成登录。首轮失败原文另存，没有删除或改写为通过；最终设置退出0、密码提示登录后用户编号0和`/root`核对通过。

配置只涉及屏幕和三针口两组登录服务：先保存两份原`zz-board-a-root-autologin.conf`，再新增`zz-board-a-password-login.conf`并移除本轮前的两份root自动登录补充文件。屏幕生效命令为`/sbin/agetty --noclear - $TERM`；三针口为`/sbin/agetty --noclear %I 1500000 linux`，都不含`--autologin`。三针口原厂商`autologin.conf`仍保持原字节，由后加载的新文件覆盖生效命令，避免退回lckfb自动登录；原厂商服务模板未修改。

重新加载服务配置、同步后重启。重启编号由`3bdb939a-ec87-4ed2-a3fe-029ec5fa19a9`变为`3df94f31-f1dc-43b7-a6c3-e2facf2895f1`。实际两个本地终端都只有等待输入的agetty，没有已登录Bash，`who`无本地登录会话；屏幕实际缓冲区读到`localhost login:`。核对脚本退出0。重启后在独立交互终端调用系统`/bin/login root`，先看到密码提示，再输入指定密码，实际进入root、用户编号0及`/root`，退出0；这不代表已经在实体键盘上人工输入验收，也不表示开启了SSH密码认证。

现在使用屏幕或三针调试口时，在`login:`处输入`root`回车，再在`Password:`处输入已设置的密码并回车；输入过程不显示字符，验证成功后位于`/root`，管理员命令不需sudo。用户不必另行修改密码。WindTerm“lckfb-板A”继续密钥连接，SSH原root登录策略、root公钥、WindTerm配置、普通用户目录、Qt应用及串口/CAN配置保留。

本次有限恢复输入只保存受影响的登录配置，不含密码、密码摘要或`shadow`文件，也不能恢复此前旧密码；恢复配置不等于撤回本轮密码设置。重启期间连接中断与超时、首轮密码失败及最终成功均有原文。本次没有实做回退或新增外设通信验收。

[本地登录方法与完整证据](../04_references/workspace_setup/root_password_login_board_a_20261102/README.md) · [有限恢复输入](../07_backups/root_session/board_a_password_login_20261102/README.md)。本节保留本地密码登录设置的经过，随后开放的SSH远程密码登录见下一节。

<a id="board-a-ssh-password-20261102"></a>
#### 8.1.4 新电脑使用root系统密码远程登录（2026-11-02，已实际核对）

用户在新电脑WindTerm输入root密码时提示身份验证不正确。只读核查确认板A原`PermitRootLogin without-password`禁止root用密码远程登录，原电脑则使用已登记密钥；板端日志也出现root密码失败。用户随后明确选择远程密码方式，希望各台电脑输入同一账号密码即可连接。

本轮核对板A序列号`3bf8222fe1417f6a`及既有主机指纹，只新增`/etc/ssh/sshd_config.d/20-board-a-root-password.conf`，设置`PermitRootLogin yes`；原密码认证及密钥认证开关都是yes，继续保留。新增文件原先不存在，只记录缺省状态及原主配置指纹作为恢复依据，不备份整套配置或认证文件。语法和来源条件检查后重新加载SSH服务，无需重启板卡。

已有系统密码保持原值。测试从项目外本机加密记录读取密码，经客户端真实密码提示输入，强制禁用密钥及键盘交互。有线`192.168.100.123`、无线`192.168.3.38`两次均完成密码登录，板A身份、root用户编号0、`/root`及实际退出0通过；板端分别记录`Accepted password`。随后原电脑另建密钥连接也通过，原授权文件、系统密码文件、SSH主配置和Samba配置的内部前后比较一致，比较用数据未输出或归档。

新电脑WindTerm选择SSH、端口22、用户名root及密码认证，输入原来设置的root系统密码。IP栏只填地址，不填共享目录或反斜杠。只要该电脑能连到板A，便不需要逐台登记密钥；无线IP可能重新分配。Samba共享密码仍由其单独管理，本轮没有重设密码。新电脑实际界面重连仍由用户核对，本轮只登记本机经两种网络的纯密码连接通过，未重启或执行回退。

[本轮证据与使用方法](../04_references/workspace_setup/ssh_password_board_a_20261102/README.md) · [有限恢复输入](../07_backups/root_session/board_a_ssh_password_20261102/README.md)。板B没有操作，不新增Qt或仪器验收结论。

## 9. 项目临时文件清理（2026-10-22）

第一轮删除 93 个文件，共 131,846,672 字节（约 125.7 MiB），包括根目录 3 个空临时文件、重复镜像 / 安装包、旧预览及过期构建输出；正式备份 59 项校验清单条目核对通过。`06_programs/.gitignore` 增加 `build-*/`，防止误建的 Qt shadow-build 目录进入 Git。

第二轮按用户要求将关键日志、原始输出、必要方法原稿及唯一旧源码正式归档：111 个文件逐字节 SHA-256 核验后移出 `temp/`，45 个已在正式图片或 RT 原始 ZIP 中存在的文件核对后去重，127 个过期辅助文件和可重建产物删除。本轮处理的原临时文件共 283 个、9,148,196 字节。来源、目标、用途分类和校验值见[归档与清理清单](../04_references/workspace_setup/20261022_temp_archive_manifest.json)。清单记录历史来源路径，当前使用下表正式入口。

| 需要做什么 | 当前入口及用途 |
| --- | --- |
| 查SN100源码和Qt依赖 | [SN100工程说明](../06_programs/sn100_host/README.md)及[当前Qt环境](20261022_qt_environment.md) |
| 查 RT 测量与镜像验证 | [实时内核记录](20261022_preempt_rt.md)、[构建核查日志](../04_references/preempt_rt/work_logs/)、[方法原稿](../04_references/preempt_rt/methods/)、[控制台与调优核查](../04_references/preempt_rt/console/)；原始普通 / RT 数据继续使用既有 ZIP，恢复仍使用 `07_backups/preempt_rt/` |
| 查串口历史收发与启动原始数据 | [2026-10-21 串口证据](../04_references/uart/20261021/)，原始接收二进制、日志及历史脚本同目录；当前接口测试仍使用 [C 版源码](../05_tools/tests/uart/uart4_loopback.c) |
| 查主目录变更与 root 会话核查 | [主目录日志](../04_references/workspace_setup/home_path_20261022/)、[会话日志](../04_references/workspace_setup/root_session_20261022/)；恢复脚本继续在 `07_backups/home_path/`、`07_backups/root_session/` |
| 查此前 Samba 双向测试文本 | [电脑写入文本](../04_references/samba/20261021/pc_to_rk.txt)、[RK 写入文本](../04_references/samba/20261021/rk_to_pc.txt)，仅对应当时验证，不代表 RT 内核下已复测 |

当前SN100的ARM64依赖单独放在`temp/sn100_host/arm64_sysroot/`，基础恢复输入见[SN100专用Qt依赖](../07_backups/qt_environment/sn100_base_arm64/README.md)。当前恢复入口为正式SN100工程的`prepare_arm64.sh`。

`.gitattributes` 对原始证据、可编辑 SVG 和历史源码保留字节，正式镜像和压缩备份继续使用 Git LFS。本次仅整理项目文件，未运行历史脚本、编译、连接或修改 RK；后续新运行仍会在 `temp/<program>/` 生成当次输出，重要证据再按事项归档。

### 9.1 根目录复核（2026-10-23）

本次在原项目根目录核查进度和文件用途。基础开发链路已有实测；SN100 正式移植目录仅有 `AGENTS.md`，业务模块尚未实现或验收。RT 长测、恢复及所用外设复测仍待完成；本次未连接设备或补做硬件测试。

旧 `SN100_host/` 下重新出现一份 `Seerain450_PC/SeeRain450.pro.user`，共 22,766 字节。逐行比较后确认，与参考工程中的配置仅相差 Qt Creator 自动写入的保存时间，实际设置相同。删除此副本及 3 个空目录，根目录不再保留旧入口。原文件和保留副本的 SHA-256、比较边界及清理结果追加至[原工程位置记录](../04_references/sn100_host/reference_location.json)。

参考工程仍有 3567 个文件、1,160,786,644 字节，与原清单一致；压缩包、主工程、入口和接收器的 SHA-256 均与原清单相同。本次未改参考工程、正式源码或备份。此处为当时的目录核对结果；2026-10-27清理后，SN100依赖按上文专用目录和当前工程说明恢复。


<a id="qt-demo-removal-20261027"></a>
## 2026-10-27：删除Qt独立显示示例，保留SN100专用依赖

用户明确要求删除截图所示的Qt显示/鼠标/键盘示例、所有相关目录、照片和说明；随后进一步要求取消任何将示例目录作为备用依赖来源的路径，需要的依赖归入SN100相关目录。本轮直接执行已授权清理，没有追加确认，也没有扩大为卸载Qt开发工具或删除SN100业务环境。

先核对源码、脚本、原环境记录、恢复输入、图片及引用和正在运行的进程。旧SN100 shell/prepare_runtime.sh在独立加载器归档缺失时，曾从旧临时目录复制备用加载器；SN100当前已有带SHA-256的board_loader.tar.gz，因此移除整个备用分支，缺归档时明确停止。SN100基础Qt头文件和运行库6个文件迁入[专用依赖目录](../07_backups/qt_environment/sn100_base_arm64/README.md)，逐文件指纹前后一致；prepare_arm64.sh、history/build.sh及shell/prepare_runtime.sh同步新位置。源码和资源的原有未提交/并行修改保留。

### 实际删除和保留范围

- 删除独立示例工程目录（含旧源码、个人工程配置和README）、专用显示/构建/调试证据目录、专用Windows工程设置备份及调试工具归档，共4个目录；并删除20张原图/标注PNG/SVG。本组共69文件、5,953,965字节。
- 删除示例的本机临时依赖目录，共1,934个常规文件、22个Linux符号链接，常规文件107,557,326字节。Linux链接在WSL中按链接本身处理，不追随链接到其他位置。
- 只读确认RK的示例服务未运行、无示例及目录内辅助进程后，删除其独立部署目录，共33个常规文件、31,840,006字节；未操作SN100运行/数据目录，未更改系统包、内核、网络或显示配置。
- 删除示例的使用步骤、调试教程、图片说明和导航；原Qt环境记录缩为SN100版本、依赖和中文输入资料。保留SN100原版Qt工程设置图、WSL/终端图片、全部业务图片和其他实验文件。
- 保留SN100现用的独立arm64_sysroot，以及SQL、报表、网络、字体、SVG、输入法等正式恢复输入。共用基础依赖只移动归属，不改字节。历史固定来源、校验和SN100旧版本恢复包保持原件，历史路径不再作为当前命令。

照片整理窗口同轮将图片分类到中文目录，本轮沿用新位置删除对应20张图片，未撤回分类。现存229个归类文件逐一比对原文件指纹不变；图片导航计数及当前移动对照同步，SN100原工程截图仍保留。初次分类时的249项历史记录保持当时范围，不改写成从未存在被删除文件。

### 核对结果与边界

所有递归删除/移动均先检查解析后的绝对路径位于本项目指定目录或板端明确的独立应用目录。电脑侧与板端分别在各自同一shell内检查并删除，不按模糊Qt关键词清空系统文件。删除的源码和正式证据已有Git历史，临时产物可重建；本次按用户要求不再复制一份示例备份。

3份受影响脚本的bash语法检查通过；在全新SN100隔离目录实际运行prepare_runtime.sh，从新专用归档完整恢复基础Qt、SQL/SQLite、报表、字体、网络、SVG和检查用加载器。所有归档校验通过，恢复的Core/Sql/Svg库为AArch64，加载器指纹匹配；示例目录已不存在，恢复仍成功。完成后仅删除本轮新建的重复验证目录，SN100现用依赖目录未改。此为依赖恢复核对，不是新应用构建或RK业务验收。

当前维护的源码/脚本、计划、说明和图片导航不再依赖已删目录，迁移后的压缩包及deb仍匹配Git LFS规则。清理清单、逐文件指纹和依赖恢复日志在temp/qt_cleanup_20261027/；当前资料入口为[SN100 Qt环境](20261022_qt_environment.md)及[工程说明](../06_programs/sn100_host/README.md)。没有运行新的Windows/WSL产品测试，也没有新SN100功能或仪器通过结论；不自动提交或推送Git。

<a id="board-b-unified-login-20261102"></a>
## 2026-11-02：板B统一开机、远程和共享的root登录

用户在板B共享无法访问后，要求与板A已记录的相关配置一致，包含共享与开机方式，密码采用本轮指定的全小写值。本次只操作当前连接的板B，序列号`8314b1e8ffd0321f`、SSH指纹`SHA256:S4ihaw0V/Z8w7v3VlJOQEFLEH3Hp6nr3KjiUTxanwPg`已核。板A原有配置仅作方式对照，本次没有重新核验或修改其密码。

**开机怎样使用：**屏幕或1500000速率三针调试口看到`login:`后输入root，再输入本轮指定密码；登录后进入`/root`。没有设置免密码自动登录，也没有把其他系统账号的主目录改为`/root`。通过SSH也可输入相同root系统密码登录，原有密钥仍可用。

**写入范围：**通过加密SSH标准输入执行`chpasswd`设置root密码；新增屏幕与三针口的`zz-board-b-password-login.conf`，覆盖串口原自动登录命令而保留原文件；新增`/etc/ssh/sshd_config.d/20-board-b-root-password.conf`，允许root密码认证。root主目录原本就是`/root`。原SSH主配置、密钥、普通用户目录、Qt应用和接口启动清单保留。共享只新增root账号及修改允许账号，详见[同一共享记录](20261021_Samba共享文件夹.md#board-b-root-share-20261102)。

**实际验证：**上位机先正常退出；重启编号由`a97faffb-7dca-4520-b226-84bcbde03700`变为`6193ae49-c7cc-492a-a5cb-7ccddbe1436d`。重启后两终端只有等待登录的agetty、没有已登录Bash，屏幕缓冲区实际读到`login:`。再运行系统`/bin/login root`，观察密码提示后输入，用户编号0和`/root`核对通过；此为系统登录检查，不声称实体键盘输入已由工具验收。另强制禁用密钥，纯密码SSH实际通过；普通密钥SSH仍通过。系统和硬件时钟重启后保持正确，断电保持另验。

上位机随后按原lckfb用户重新启动通过，仍是ef294a92，未设置上位机开机自启或改业务代码；没有新增仪器结果。实际输出、脚本及有限恢复输入见[板B独立证据](../04_references/workspace_setup/root_login_board_b_20261102/README.md)和[登录开关恢复输入](../07_backups/root_session/board_b_unified_login_20261102/README.md)。密码只在项目外本机加密保存，不保存旧密码、密码摘要、认证数据库或私钥；恢复只能撤回新增开关，不能恢复旧密码。

<a id="board-b-wifi-20261102"></a>
### 同日：板B连接503无线网络并测试局域网共享

用户先询问有无WiFi，随后明确要求启动并连接。板B原有wlan0硬件、NetworkManager和无线开关，原先没有保存WiFi连接；电脑当前在503，板B也能扫描到。按同一局域网目的新增`board_b_wifi_503`，UUID为`2e1ef016-7e35-4008-95a0-bc6f4cd0f2af`；从Windows当前503配置在内存取出密码，通过SSH标准输入交给NetworkManager，没有让用户重新输入、没有打印或归档密码。

首轮取得`192.168.3.59/24`，网关`192.168.3.1`。重连时多次出现获取地址超时，首次无线共享失败；固定为电脑接入点、关闭省电和改变地址申请标识均未解决，已恢复默认值。重新关闭/开启无线后，等待设备恢复再连接，地址恢复、路由器3次通信全部收到；无线共享root实际登录、双向传文件和上位机文件可读均通过。失败和修后通过分别保留，不宣布所有重连异常根因已解决。

该连接允许自动连接，正常凭据只保存在板B网络服务管理的root专用配置中，含凭据的YAML不复制到仓库。有线`192.168.100.123`及原有线配置保留；本次没有设固定无线IP，路由器后续可能分配其他地址。最终重启自动连接与无线复测结果在本节后续补充，未做断电、长期无线稳定性或吞吐量验收。过程与日志见上述板B独立证据，恢复范围见同一归档说明。

**最终复测：**再次正常关闭上位机后重启，编号从`6193ae49-c7cc-492a-a5cb-7ccddbe1436d`变为`53e37a87-d395-42bb-bde7-4835645b3bcb`。未再次输入无线密码，板B自动连接503并取得`192.168.3.59/24`，网关3次通信全部收到；两个本地终端仍等待密码登录，系统及硬件时钟保持正确。Windows从无线IP再次root登录、双向传文件逐字核对、读取上位机程序路径均通过。最后以原lckfb用户恢复打开上位机，主程序和原接口启动文件指纹保持原值。重连异常的早期失败保留；只确认本轮恢复及开机连接，不声称长期或所有异常重连已通过。

17:23北京时间最终快照中自动校时为`NTPSynchronized=yes`，上位机运行、WiFi连接和原有线地址均正常。含无线凭据的Netplan文件只保留在板B网络服务原位，权限root:root 600，不复制到工程；新增连接可在有线下按UUID单独撤回。正式证据与恢复清单均核对，保留其他未提交工作，未提交或推送Git。

<a id="board-b-windterm-20261102"></a>
## 8.2 板B的WindTerm入口LCKFB-B（2026-11-02）

用户要求重新配置LCKFB-B。检查本机保存的配置时未发现该名称；用户正常退出WindTerm后仍为原6个会话，因此本次新增独立入口，保留原会话。只读SSH实查硬件序列号`8314b1e8ffd0321f`，与板B记录及主机指纹一致。

新增会话名为`LCKFB-B`，唯一编号为`105421f6-cf23-42fe-b8b2-51f3c85917ba`。采用与板A现有入口相同的Shell终端方式，调用Windows已有SSH程序，以本电脑已有密钥连接`root@192.168.100.123`；双击后进入`/root`，无需先执行sudo。此方式不提供WindTerm原生SSH会话的文件管理面板。板B原系统密码及板端设置保留。

为避免两块板复用同一IP时混淆，使用独立文件`C:\Users\52400\.ssh\known_hosts_rk3576_board_b`及别名`rk3576-board-b`核对板B公开身份；主机指纹为`SHA256:S4ihaw0V/Z8w7v3VlJOQEFLEH3Hp6nr3KjiUTxanwPg`。原6个会话及Windows原known_hosts未覆盖。配置文件格式依据[WindTerm作者关于user.sessions的说明](https://github.com/kingToolbox/WindTerm/discussions/69)。

**实际验证：**使用新会话保存的同一程序和参数分配交互终端连接，只读输出板B序列号、root、用户编号0、主目录及当前目录`/root`，正常退出0；会话文件回读正确，原6个会话逐项比较一致，新增后7个。WindTerm界面双击连接没有代验，不新增Qt、串口或仪器通过结论。无线旧地址`192.168.3.59`本次连接超时，因此入口使用有线。

[实际结果与操作方法](../04_references/workspace_setup/windterm_board_b_20261102/README.md) · [有限恢复输入](../07_backups/root_session/board_b_windterm_20261102/README.md)。仅归档新增会话和公开主机身份，没有保存密码、私钥或整份含认证数据的配置；临时脚本在`temp/windterm/board_b/20261102/`。保留已有未提交修改，未提交或推送Git。

<a id="board-b-userdata-samples-20261102"></a>
## 2026-11-02：删除板B的三个厂家测试素材

用户了解文件用途后，明确要求按已说明的范围删除三个测试素材，保留`lost+found`和`/userdata`目录。17:28:12（北京时间）通过严格SSH主机身份核对连接当前板卡，硬件序列号为`8314b1e8ffd0321f`，确认是板B；主机指纹为`SHA256:S4ihaw0V/Z8w7v3VlJOQEFLEH3Hp6nr3KjiUTxanwPg`。板A未操作。

删除前确认三个对象均为普通文件、不是符号链接，且文件指纹与官方系统源码`/home/qyq/TaishanPi-3-Linux/device/rockchip/common/extra-parts/userdata/normal/`中的素材一致。现有系统源码及正式源码归档已保留这些原件，本次不重复备份测试素材，不修改系统配置或应用环境。

| 已删除的文件 | 大小（字节） | SHA-256文件指纹 |
| --- | ---: | --- |
| `/userdata/200frames_count.h264` | 87,402 | `41705b195050b4e9f00f30666f20be89e71f0bd80707f8dfda38f9dd1588c3c4` |
| `/userdata/belle-nuit-testchart-1080p.png` | 81,582 | `bf079afb9edf17761e36d360f2124dc3ed8ab5b52c7be67b505d2f6a74f29eed` |
| `/userdata/piano2-CoolEdit.mp3` | 101,760 | `8e2a2c33adb76df6e098e79fbb1bb5a2ebdfd019d9bb955ac655c85912b9dc64` |

只对上述三个绝对路径执行不递归删除，共270,744字节；执行后逐项确认文件及同名链接均不存在。`/userdata`保留，`lost+found`、`shared`内的目录/文件属性和文件内容、隐藏标记`.resized`的属性和内容删除前后一致，SSH退出0。日志在`temp/userdata_cleanup/board_b_20261102/cleanup.log`。

本次核对范围为RK板上的文件清理及保留资料的一致性，没有重启、编译、停止或重新启动上位机，也没有新增应用功能或真实仪器验证结论。已有其他未提交修改保留，未提交或推送Git。
