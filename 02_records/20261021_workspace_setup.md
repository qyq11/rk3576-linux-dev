# 程序目录与 WindTerm WSL 工作目录

日期：2026-10-21。环境：Windows、`C:\WindTerm\WindTerm.exe`、WSL 2 Ubuntu-24.04，Ubuntu 用户 `qyq`。

当前有效的 Windows 普通权限启动配置见第 6 节；原 VM 的免密连接会话见第 7 节；RK 的 `lckfb` 会话自动进入 root 终端见第 8 节。前面的目录设置与升级步骤保留为当时的操作记录。

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

2026-10-21，用户反馈每次打开 WindTerm 都出现管理员授权窗口。[查看用户提供的原始照片](../03_photos/20261021_windterm_uac_01.jpg)，照片原文件已归档，字节校验与用户原件一致。弹窗显示程序为 `C:\WindTerm\vendors\gsudo\gsudo.exe`。

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

root shell 正常退出并关闭测试连接；[原始交互日志](../04_references/workspace_setup/root_session_20261022/interactive_20261022.log)保留终端控制字符，按回车分行后逐项核对上述结果。[配置与核查日志](../04_references/workspace_setup/root_session_20261022/)已正式归档，一次性辅助脚本已清理；回退仍使用前述 `07_backups/root_session/` 正式资料。本次验证了已保存的会话配置与相同命令的实际交互提权；没有启动 WindTerm 并采集该会话界面，因此首次 GUI 连接结果以实际打开该会话为准。

以后打开 WindTerm，双击原来的 **`lckfb`** 会话，认证成功后约 1 秒自动进入 root，提示符应为 `root@localhost:~#`。此时终端命令以 root 执行，主目录为 `/root`；`exit` 可返回原 lckfb shell。SFTP 仍使用会话的 lckfb 身份；AI 构建部署工作流继续按项目约定使用普通用户。

`lckfb` 账号主目录仍为 `/home/rkshare/lckfb`，旧 `/home/lckfb` 链接未重建，详见[主目录统一记录](20261021_Samba共享文件夹.md#7-账号主目录统一到共享目录2026-10-22已完成)。Windows 上的 WindTerm 继续普通权限启动，不会因此触发 gsudo 或 Windows 管理员授权。

## 9. 项目临时文件清理（2026-10-22）

第一轮删除 93 个文件，共 131,846,672 字节（约 125.7 MiB），包括根目录 3 个空临时文件、重复镜像 / 安装包、旧预览及过期构建输出；正式备份 59 项校验清单条目核对通过。`06_programs/.gitignore` 增加 `build-*/`，防止误建的 Qt shadow-build 目录进入 Git。

第二轮按用户要求将关键日志、原始输出、必要方法原稿及唯一旧源码正式归档：111 个文件逐字节 SHA-256 核验后移出 `temp/`，45 个已在正式图片或 RT 原始 ZIP 中存在的文件核对后去重，127 个过期辅助文件和可重建产物删除。本轮处理的原临时文件共 283 个、9,148,196 字节。来源、目标、用途分类和校验值见[归档与清理清单](../04_references/workspace_setup/20261022_temp_archive_manifest.json)。清单记录历史来源路径，当前使用下表正式入口。

| 需要做什么 | 当前入口及用途 |
| --- | --- |
| 继续修改、编译或调试 Qt 程序 | [Qt 工程](../06_programs/qt_demo/qt_demo.pro)、[当前源码](../06_programs/qt_demo/app_main.cpp)；按 [Qt 记录第 8 节](20261022_qt_environment.md#8-清理后文件入口与重新构建)重新构建，旧 EXE / ELF / 对象文件已清理 |
| 查旧 Linux 专用 Qt 源码 | [linux_main_20261022.cpp](../06_programs/qt_demo/history/linux_main_20261022.cpp)，仅历史原件，不参与当前 `.pro` 和 `build.sh` 构建 |
| 查 Qt 显示、输入及调试证据 | [Qt 原始证据目录](../04_references/qt_environment/)，具体日志、状态及图片见 [Qt 文件入口](20261022_qt_environment.md#8-清理后文件入口与重新构建) |
| 查 RT 测量与镜像验证 | [实时内核记录](20261022_preempt_rt.md)、[构建核查日志](../04_references/preempt_rt/work_logs/)、[方法原稿](../04_references/preempt_rt/methods/)、[控制台与调优核查](../04_references/preempt_rt/console/)；原始普通 / RT 数据继续使用既有 ZIP，恢复仍使用 `07_backups/preempt_rt/` |
| 查串口历史收发与启动原始数据 | [2026-10-21 串口证据](../04_references/uart/20261021/)，原始接收二进制、日志及历史脚本同目录；当前接口测试仍使用 [C 版源码](../05_tools/tests/uart/uart4_loopback.c) |
| 查主目录变更与 root 会话核查 | [主目录日志](../04_references/workspace_setup/home_path_20261022/)、[会话日志](../04_references/workspace_setup/root_session_20261022/)；恢复脚本继续在 `07_backups/home_path/`、`07_backups/root_session/` |
| 查此前 Samba 双向测试文本 | [电脑写入文本](../04_references/samba/20261021/pc_to_rk.txt)、[RK 写入文本](../04_references/samba/20261021/rk_to_pc.txt)，仅对应当时验证，不代表 RT 内核下已复测 |

`temp/` 当前仅保留约 102.6 MiB 的 `qt_demo/sysroot/` 和一份说明。它是电脑端 Qt ARM64 编译依赖；如以后主动删除，在 WSL 的 `06_programs/` 执行下列命令，可从[正式构建输入](../07_backups/qt_environment/qt5_arm64/source.json)核验并重建：

```bash
bash qt_demo/prepare.sh
```

`.gitattributes` 对原始证据、可编辑 SVG 和历史源码保留字节，正式镜像和压缩备份继续使用 Git LFS。本次仅整理项目文件，未运行历史脚本、编译、连接或修改 RK；后续新运行仍会在 `temp/<program>/` 生成当次输出，重要证据再按事项归档。

### 9.1 根目录复核（2026-10-23）

本次在原项目根目录核查进度和文件用途。基础开发链路及 Qt 最小示例已有实测；SN100 正式移植目录仅有 `AGENTS.md`，业务模块尚未实现或验收。RT 长测、恢复、外设复测及最终跨平台示例的 HDMI/USB 对照复测仍待完成；本次未连接设备或补做硬件测试。

旧 `SN100_host/` 下重新出现一份 `Seerain450_PC/Seerain450_PC/SeeRain450.pro.user`，共 22,766 字节。逐行比较后确认，与参考工程中的配置仅相差 Qt Creator 自动写入的保存时间，实际设置相同。删除此副本及 3 个空目录，根目录不再保留旧入口。原文件和保留副本的 SHA-256、比较边界及清理结果追加至[原工程位置记录](../04_references/sn100_host/reference_location.json)。

参考工程仍有 3567 个文件、1,160,786,644 字节，与原清单一致；压缩包、主工程、入口和接收器的 SHA-256 均与原清单相同。本次未改参考工程、正式源码或备份。`temp/qt_demo/sysroot/` 是当前 ARM64 Qt 构建使用的依赖，继续保留；此前已归档的证据和恢复材料仍沿用本节已有入口。
