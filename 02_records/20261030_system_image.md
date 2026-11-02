# 给新板准备Ubuntu 24.04整机镜像

2026-10-30｜立创泰山派3M RK3576｜目标系统：Ubuntu 24.04 Server / ARM64

**当前状态：完整编译、最终打包、镜像内容与归档恢复核对全部通过，正式`update.img`已保存。**实际系统为Ubuntu 24.04.5 LTS Server、`6.1.99-rt36-tspi`实时内核，预装固定地址`192.168.100.123`、SSH/Samba、共享及主目录和必要检查工具；不加入上位机。系统内容51项、实际镜像文件170项检查通过，包含157个匹配模块。[镜像与首次使用说明](../07_backups/system_image/ubuntu24_server/README.md)。板子当前不在身边，本轮未烧录，板端启动和服务功能待拿到板子后验证。[实施清单](../01_docs/plans/development_plan.md#system-image-new-board-20261030)

## 本次要做什么

**最新确认优先于后文暂停时的历史范围：**用户要求完成编译，并将以前的板端准备提前放入镜像；实时内核需要，上位机不需要。有线地址沿用`192.168.100.123/24`，只连接一块板使用；板子不在身边，实际烧录另安排。基础服务指SSH远程登录、Samba文件共享、网络管理和时间同步；检查工具用于查看网络、日志和实时线程延迟，不在构建时自动操作GPIO或其他外设。Samba共享密码在新板首次使用时现场设置一次，不把旧板账号库、密码或私人密钥写入通用镜像。

给另一块同型号的新板准备完整系统镜像，并留下来源、源码版本、配置、编译命令、结果和文件指纹。用户已确认新板没有需要保留的数据。本轮只准备镜像；以后烧录会写入新板系统和数据区域，届时再核对实际对象及写入范围。

这里的“整机镜像”指最终供烧录工具使用的`update.img`；“系统源码包（SDK）”是用于生成这个镜像的代码和工具。编译完成与板端启动通过分别记录。

## 原板和原电脑环境的依据

| 对象 | 已找到的依据 |
| --- | --- |
| 原RK板系统 | 保存的[系统信息](../07_backups/preempt_rt/board_environment_20261022_111741/system_environment.txt)写明`Ubuntu 24.04.5 LTS`、ARM64，最初内核为`6.1.99`。后加实时内核的操作另见[原记录](20261022_preempt_rt.md)。 |
| 原系统配置 | [原SDK配置](../07_backups/preempt_rt/board_baseline_20261022_104356/sdk_baseline/sdk_build.config)为`tspi_3m_rk3576_ubuntu_noble_server_defconfig`，即Ubuntu 24.04 Server；设备树为`tspi-3m-rk3576-panfrost`。Server默认不安装整套桌面，但可以另外安装并运行Qt图形程序。 |
| 原VMware虚拟机 | 本机登记名为`Ubuntu 64-bit 22.04_RK3576`，位置`D:\VirtualMachines\Ubuntu22.04\Ubuntu 64-bit 22.04_RK3576.vmx`。本轮没有开机查询实际发行版小版本。 |
| 原工程和整机镜像线索 | 历史记录中工程为VM内`/home/qyq/rk3576/TaishanPi-3-Linux`；`rockdev/update.img`链接到`output/update/Image/update.img`。本轮SSH连接旧VM返回`No route to host`，未取得该旧镜像实体，因此没有把历史文件线索记作本轮镜像已准备好。 |

电脑上的编译系统版本和板子上的系统版本可以不同：本次用WSL Ubuntu 22.04编译，目标仍为板端Ubuntu 24.04 Server。

## 官方来源与本次保存位置

- [官方SDK获取说明](https://wiki.lckfb.com/zh-hans/tspi-3-rk3576/system-development-compilation/ubuntu24/sdk-setup.html)：采用官网提供的仓库同步方式。
- [官方镜像编译说明](https://wiki.lckfb.com/zh-hans/tspi-3-rk3576/system-development-compilation/ubuntu24/image-compilation.html)：选择板级配置后运行`./build.sh all`，完整产物为`rockdev/update.img`。
- 源码清单仓库：`https://github.com/jlckfb/TaishanPi-manifests.git`；官网分支`linux/tspi-3-260402`，本次固定清单提交`36d3aadff62760df0a12ebcfcfa9d1373ef66654`。实际子仓库从清单指定的立创CNB仓库获取。
- 下载入口、原安装脚本和当前清单存于[本次资料目录](../04_references/system_image/20261030/)，其中[sources.json](../04_references/system_image/20261030/sources.json)登记下载和构建状态。清单下载不等于全部源码已下载。
- 正式安装输入与最终镜像归入[07_backups/system_image](../07_backups/system_image/README.md)；临时脚本、下载及构建日志在`temp/system_image_20261030/`。

## 已实际完成的电脑准备

原有WSL为`Ubuntu-24.04`，仍用于既有项目。官网安装脚本要求Ubuntu 22.04，因此本次新增独立的`RK3576-Build-22.04`，构建源码、链接和Linux文件权限放在它自己的Linux文件系统内；没有升级或降级原有WSL。

本次取得的Ubuntu编译环境安装包为`ubuntu-22.04.5-wsl-amd64.wsl`，大小360,684,292字节。原发布方为Canonical，最终从中国科学技术大学镜像站续传；完整文件与Canonical发布的SHA-256一致：

```text
4499c4fe257f2fc83145b429ce211a0a43fd590e70d6261ede616210947d9f8f
```

这里比较的是文件指纹，用来确认下载内容完整；安装包是电脑的编译环境，不能烧录到RK。安装包与官方校验清单在[编译环境归档目录](../07_backups/system_image/build_host/)。

实际导入命令在Windows项目根目录执行：

```powershell
wsl.exe --import RK3576-Build-22.04 temp\system_image_20261030\wsl_ubuntu22 07_backups\system_image\build_host\ubuntu-22.04.5-wsl-amd64.wsl --version 2
```

专用环境内已安装Git、源码大文件工具、编译器、内核编译依赖、ARM64程序辅助执行工具和镜像打包工具；准备脚本返回`BUILD_HOST_PREPARE_EXIT=0`。系统安装步骤以该新环境的root执行，后续下载与编译使用普通用户`qyq`。生成系统镜像时需要的挂载权限仅设置在这个新建的专用编译环境内。

本机检查时C盘剩余约315 GiB；WSL可见内存约15 GiB。官网列出的参考内存为32 GB，本次后续编译从较少并行任务开始，是否成功以实际编译结果为准。

## 已完成：下载官方SDK与系统基础包

普通用户下载脚本返回`SDK_SOURCE_SYNC_EXIT=0`，71个源码项目同步完成。源码位置为新WSL内`/home/qyq/TaishanPi-3-Linux`。对应虚拟磁盘随后迁移到正式工程的`06_programs/rk3576_system/wsl_workspace/`；在资源管理器中双击[源码打开入口](../06_programs/rk3576_system/open_system_sources.lnk)即可查看源码。迁移时遇到WSL磁盘占用，停止空闲WSL实例后迁移成功；原WSL版本及内容不变。

各子仓库的实际提交已写入[固定版本清单](../04_references/system_image/20261030/manifest_pinned.xml)。3个使用Git LFS大文件的仓库均下载完整，内容与记录的文件指纹核对通过；结果保存在[lfs_verification.log](../04_references/system_image/20261030/lfs_verification.log)。

首次“源码全部未修改”检查因官方文档仓库的嵌套及PDF链接状态而停止，尚未生成压缩包。核对后确认这些状态来自官方清单安排的文档子仓库和链接；构建所用源码无修改。原始检查及[逐仓库状态](../04_references/system_image/20261030/official_worktree_status.txt)保留，没有清除这些官方文档。随后重新按该实际范围开始制作官方源码归档。

SDK使用的Ubuntu ARM64基础包`ubuntu-base-24.04.4-base-arm64.tar.gz`已另行下载，与Canonical官方发布校验清单一致；[基础包和校验清单](../07_backups/system_image/sources/ubuntu_base/)正式保存。此基础包经过SDK安装软件包后才形成完整系统，不能直接代替`update.img`。

正式文件按用户要求分开：官方源码归档放`07_backups/system_image/sources/`，实际工程及构建入口放`06_programs/rk3576_system/`，完整镜像和配套文件放`07_backups/system_image/ubuntu24_server/`。

官方文件树归档大小9,515,085,096字节，压缩包完整性检查、文件指纹及清单核对均通过，归档脚本返回`OFFICIAL_SOURCE_ARCHIVE_EXIT=0`。整包SHA-256为`b9ba2d6f458ca488b672a3e0bfd3851c0734bbcb2f4c7660a1e48c9f59d83a91`。归档包含源码文件及所需大文件，不包含可重建的Git对象数据库和后续构建产物。为满足[GitHub大文件单文件大小限制](https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-git-large-file-storage)，按每卷1 GiB保存；每卷读回核对及合并指纹核对通过后才移除重复整包。

## 历史暂停点与已保存内容

实际从Windows项目根目录启动的命令为：

```powershell
wsl.exe -d RK3576-Build-22.04 -u qyq -- bash /mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/rk3576_system/build.sh /mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/system_image_20261030/build_candidate/update.img
```

暂停前的构建入口选择原版`tspi_3m_rk3576_ubuntu_noble_server_defconfig`，在生成的配置中将Ubuntu软件源设为`mirrors.ustc.edu.cn`，并限制使用CPU0至CPU3。编译和打包通过官方`./build.sh all`执行；暂停时尚未取得完整构建的正常退出0，不能凭当时的部分输出判断整机镜像通过。恢复后改用CPU0至CPU7，并接入下文的实时配置和环境准备。

实际编译用的Ubuntu镜像生成脚本增加了有限的首次启动准备：在系统文件打包前清除编译时生成的SSH主机密钥、机器标识和随机种子，保留官方首次启动生成SSH密钥的服务，让每块新板分别生成自己的身份。原始官方源码归档在此修改前已封装完成；差异由工程中的`prepare_image_sources.py`明确维护，后续归档实际补丁。软件配置和业务功能未据此更改，新板启动及SSH连接仍待烧录后验证。

首轮引导程序编译通过，进入内核构建前因缺少`python-is-python3`配套包停止，实际退出1；[首轮日志](../04_references/system_image/20261030/build_attempt_01_missing_python_alias.log)保留。补装该包后，以相同入口开始第二轮；[安装结果](../04_references/system_image/20261030/install_python_alias.log)已保存。该包让SDK调用`python`时使用Python 3，不是给板子增加Python业务程序。

用户先要求“如果还没有开始编译就不用编译、尽快收尾”，当时编译已经开始；随后明确要求“暂停编译并收尾”。收到明确暂停要求后立即中断第二轮，外层运行进程因中断退出1。停止时内核正生成模块对象文件，不是本轮完整编译成功，也不是新增的源码编译故障。复查SDK路径及构建入口均无遗留编译进程。

暂停时已保存[暂停日志](../04_references/system_image/20261030/build_attempt_02_paused.log)、[产物存在状态](../04_references/system_image/20261030/paused_artifact_status.json)、[当时的系统配置](../04_references/system_image/20261030/paused_system.config)和[当时的内核配置](../04_references/system_image/20261030/paused_kernel.config)。当时没有自动重编或另开任务；随后用户明确要求恢复，继续过程见下文。工程中的当前配置和源码差异随最终镜像更新，历史配置另存上述资料目录。

检查时`u-boot/uboot.img`存在，大小4,194,304字节；`kernel-6.1/boot.img`、ARM64内核`Image`及两处完整`update.img`均不存在。仅有引导程序不能用于本轮整机烧录，因此未创建装有占位镜像的`ubuntu24_server/`目录。

源码分卷曾因整理脚本累计长度的判断方式而停止，原整包当时仍保留；随后按实际9卷重新读取，合并指纹与原整包一致，才移除重复整包。失败及修正结果均保留在本次资料目录；9卷源码内容没有删减。

## 板端尚未验证的范围

完整编译、最终打包和正式镜像校验已完成；新板实际烧录、首次启动、实时标志、显示、网络、SSH/Samba连接、串口及其他外设均未在本轮验收。实时内核、固定IP、SSH/Samba、共享目录和必要工具已在镜像中核对，不把文件存在和配置正确登记成服务已实际可用；Qt/SN100业务运行环境不在本次新增范围。

## 恢复编译：实时内核与基础环境

本次下载的内核提交与此前RT补丁的依据均为`bf3e03f69e6a3b766dc6356a7475e8ad64a48a98`。沿用已归档、上次已在原板启动验证的178文件补丁；文件指纹、正向应用及反向核对通过。通过新增配置片段选择实时调度、保留250 Hz及高精度计时，目标版本`6.1.99-rt36-tspi`。这说明补丁来源一致，不代替本次新板启动验证。

本轮新增的环境脚本由SDK在新根文件系统内执行：安装Samba和检查工具，设置服务开机启动，创建`/home/rkshare`和`/home/rkshare/programs`，将`lckfb`主目录登记为`/home/rkshare/lckfb`。网络使用原记录的`end0`及固定地址，不绑定旧板MAC，不设置有线默认网关。SSH主机密钥、Samba本机账号库及机器标识在各新板首次使用时生成，原板数据和认证文件没有复制。

实时内核编译返回`RT_KERNEL_BUILD_EXIT=0`，生成版本为`6.1.99-rt36-tspi`。与此前原板已验证配置按配置项比较，差异为0项；[编译日志](../04_references/system_image/20261030/build_rt_kernel.log)和[配置对照结果](../04_references/system_image/20261030/rt_config_comparison.txt)已保存。这只确认本轮编译和配置一致，未在新板运行。

完整系统首次尝试停在官方设备树工具的Go安装准备，没有进入Ubuntu组装。该工具的`go.mod`要求Go 1.24，而自动安装入口默认为1.23.8，且下载长时间无进展；中断后在专用编译环境单独安装官方Go 1.24.13。安装包与官方发布的SHA-256 `1fc94b57134d51669c72173ad5d49fd62afb0f1db9bf3f798fd98ee423f8d730`一致，安装包和发布信息保存在`07_backups/system_image/build_host/`。重试中设备树工具成功交叉编译，继续完成Ubuntu ARM64组装。[首次尝试日志](../04_references/system_image/20261030/build_full_attempt_01_go_setup.log)保留。

完整构建随后返回`SYSTEM_IMAGE_BUILD_EXIT=0`，[完整编译日志](../04_references/system_image/20261030/build_full_environment.log)已保存。继续检查首次启动与打包内容：将官方通过重配置软件包生成SSH密钥的服务改为直接运行`ssh-keygen -A`，并确保远程登录服务等待密钥生成完成。该命令只生成缺失的主机密钥，具体新板启动结果仍待验证。

首次最终检查发现3处共享目录归属错误，并发现155个内核模块的原始文件指纹与打包后不同；[首次检查](../04_references/system_image/20261030/rootfs_content_first_check.json)和[当次打包日志](../04_references/system_image/20261030/finish_image_first_check.log)保留。没有把这份镜像作为最终镜像发布。

目录问题来自官方`99-owner.sh`：电脑编译用户与板端用户的数字编号同为1000，脚本先把这些文件改归管理员，再只恢复`/home/用户名`形式的目录。`/home/rkshare/lckfb`不是这种一级结构，导致共享目录仍归管理员。本次增加最后执行的`zz-rk3576-environment.sh`，只针对带本次环境标记的Ubuntu新系统，在原脚本之后恢复共享目录及其中文件的用户、组和权限。

模块差异来自官方`91-modules.sh`会去掉调试信息。检查改为用官方同一工具对编译产物的临时副本执行相同处理，再逐文件比较；原编译产物不改动。同时检查模块的ARM64架构和实时内核版本标记。官方另装在版本目录外的两个无线驱动也纳入核对，不凭版本目录名称判断它们已匹配。

## 最终结果与正式文件

2026-10-30完成最终打包并归档。系统基础包原为24.04.4，经过官方脚本更新软件包后，实际镜像`os-release`为`Ubuntu 24.04.5 LTS`。采用Server配置，未安装SN100上位机，保留官方硬件和多媒体配套依赖。

| 内容 | 实际保存位置与结果 |
| --- | --- |
| 可直接烧录的整机镜像 | [update.img](../07_backups/system_image/ubuntu24_server/update.img)，3,032,177,283字节，约3.03 GB。 |
| 同内容长期归档 | `07_backups/system_image/ubuntu24_server/update.img.gz`，709,013,848字节；压缩完整性检查通过，实际恢复后的指纹与编译输出一致。 |
| 匹配的恢复输入 | 同目录保存启动文件、ARM64内核、设备树、157个模块的压缩包、系统/内核配置、源码版本清单及环境说明。具体恢复范围见[目录说明](../07_backups/system_image/ubuntu24_server/README.md)。 |
| 文件指纹和版本 | 同目录`SHA256SUMS`和`image_manifest.json`；[编译输出原始指纹](../04_references/system_image/20261030/compiled_image_source.json)另存证据。 |
| 官方原始源码 | `07_backups/system_image/sources/`的9个分卷、固定版本及Ubuntu ARM64基础包，均已核对。 |
| 实际可继续修改的工程 | 专用WSL内`/home/qyq/TaishanPi-3-Linux`，工作盘存于`06_programs/rk3576_system/wsl_workspace/`，Windows中可双击[源码入口](../06_programs/rk3576_system/open_system_sources.lnk)。 |

最终`update.img`的SHA-256为：

```text
cd680eda3a96ff9dc6dbb4e3ceea6924474e68ce8b39702b4a78b4d662375a4c
```

首轮完整构建曾在`temp/system_image_20261030/build_candidate/update.img`产生检查前的版本；它的指纹为`e3cb140934ced0d60ef2b1ab221fc2ce67df8ee26d714ba2655628c87c18d449`。该中间文件已在2026-11-02清理中删除，不能替代上述已修正归属的正式文件；原始日志及指纹记录保留。

最终验证范围：

- [x] 官方完整构建正常退出0；最终修正后重新打包正常退出0，[最终打包日志](../04_references/system_image/20261030/finish_image.log)保留。
- [x] 系统内容51项通过、0项失败：目标发行版、软件包全部配置完成、用户主目录、共享配置、目录归属与权限、固定地址、服务开机链接、现场设置共享密码入口、生成新身份的准备及实时模块。[检查结果](../04_references/system_image/20261030/rootfs_content_verification.json)。
- [x] 155个内核模块及2个附加无线驱动，与本轮编译产物按相同去调试处理后逐文件一致；全部模块的标记为`6.1.99-rt36-tspi SMP preempt_rt mod_unload aarch64`。
- [x] 只读访问实际根文件系统镜像，170个关键文件逐一核对通过，包含全部157个模块；没有启动RK系统或服务。[镜像文件结果](../04_references/system_image/20261030/packed_image_file_verification.json)。
- [x] 从`boot.img`提取内核、设备树及资源文件，与编译产物逐文件一致；[启动镜像信息](../04_references/system_image/20261030/boot_fit_information.txt)和[归档日志](../04_references/system_image/20261030/archive_final_image.log)保留。
- [x] 完整镜像为官方`RKFW`格式，官方打包中的CRC/MD5生成通过；压缩归档完整，Windows实际解压后的3.03 GB文件指纹与原输出一致。[恢复结果](../04_references/system_image/20261030/restore_final_update.log)。
- [ ] 新板烧录、首次启动、实时标志、固定地址、SSH登录、Samba读写及外设运行：板子不在身边，均待验证。

本次新增环境脚本、末尾目录修正、首次启动服务、RT配置片段和完整构建入口集中在[系统工程](../06_programs/rk3576_system/README.md)。后续新构建的检查报告放入新输出目录`checks/`，不覆盖本次正式证据。

本地保留可直接烧录的展开镜像；Git按LFS规则保存同内容压缩归档及匹配二进制文件，展开文件由明确的忽略规则排除，可按指纹恢复。本轮没有自动提交或推送Git。密码、旧板账号库、业务数据及私人密钥没有加入本次定制输入。

拿到板子后的顺序是：确认设备与写入范围 → 整机烧录正式`update.img` → 核对启动和实时标志 → 核对固定地址与SSH → 在新板运行`sudo rk3576-first-setup`设置Samba密码 → 实测共享读写及所需外设。每一步单独记录，不将本次编译结果当作板端验收。

<a id="cleanup-and-push-20261102"></a>
## 2026-11-02：清理中间文件并保存到Git

用户明确要求清理中间文件、缓存，然后提交并推送。操作前核对本地及远端分支均位于`eac9a87`；确认专用WSL内没有编译任务，先核对正式镜像、源码分卷和SN100匹配恢复输入。41项归档指纹均与清单一致，其中大文件用Git LFS暂存时实际计算的内容指纹比较，展开镜像重新读取计算指纹。

- Windows项目`temp/`内逐文件删除1,122项，共6,635,066,233字节：旧候选镜像、两轮SN100编译对象及Qt生成文件、临时应用和测试程序、打包暂存副本及软件仓库索引。没有递归删除整个目录；依赖、运行库、匿名运行数据、源码和原始证据保留。
- 专用WSL内逐文件删除11,087项，共4,042,036,383字节：内核与附加无线驱动的Git未跟踪编译对象、依赖/命令缓存，以及内部RKAF打包文件。保留最终RKFW镜像、模块、配置、源码和已配置系统目录；清理前后两个源码仓库的已跟踪修改状态及内核配置指纹均一致。
- 另外清理约79.43 MB的Go编译缓存，并执行专用环境的APT下载缓存清理；没有卸载编译器或库。合计删除约10.76 GB文件数据，其中WSL部分增加的是虚拟磁盘内部可用空间，不代表Windows上的VHDX文件会立即缩小。

逐文件清单、实际结果、归档核对及执行脚本统一放在[清理证据](../04_references/workspace_setup/cleanup_20261102/README.md)。清单目标剩余数均为0。正式`update.img`和压缩归档保留；本次清理不改变镜像字节，不增加新板启动、服务或硬件通过结论。

补齐系统镜像证据、配置与补丁的字节保留规则，以及Linux首次启动入口的换行规则，避免Windows检出后改写原件或导致脚本无法启动。源码和维护说明的空白检查通过；原始日志、清单及配置保持原字节，不按格式警告重写。

提交范围包括新板镜像、官方源码归档、构建输入、对应记录，以及此前已完成RK软件验证但尚未提交的SN100更新及配套证据。临时目录、运行数据、WSL工作盘及重复展开镜像不进入Git；压缩归档和匹配二进制按LFS规则保存。提交与推送的最终结果在完成后记录。
