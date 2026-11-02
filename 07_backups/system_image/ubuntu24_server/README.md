# 新板整机镜像：Ubuntu 24.04.5 Server与实时内核

适用板子：立创泰山派3M RK3576。2026-10-30完整编译、最终打包与镜像内容核对通过。板子不在身边，本轮没有烧录，没有验证新板启动、网络、共享及外设。

**可直接交给烧录工具的文件是本目录`update.img`**，大小3,032,177,283字节，约3.03 GB。它已从压缩归档实际恢复，并与编译输出的SHA-256文件指纹一致：

```text
cd680eda3a96ff9dc6dbb4e3ceea6924474e68ce8b39702b4a78b4d662375a4c
```

| 文件 | 用途 |
| --- | --- |
| `update.img` | 整机烧录文件，包含引导、内核、系统及配套分区。展开文件在本地正式目录保留。 |
| `update.img.gz` | 同一整机镜像的压缩归档，709,013,848字节，使用Git LFS保存；可恢复上面的完整文件。 |
| `SHA256SUMS`、`image_manifest.json` | 文件指纹、大小、源码版本及已验证范围。先恢复展开镜像，再核对完整清单。 |
| `boot.img`、`uboot.img`、`MiniLoaderAll.bin`、`parameter.txt`、`package-file` | 同轮编译的启动文件、分区安排和整机打包清单，供后续恢复、定位问题使用。整机烧录入口仍为`update.img`。 |
| `Image`、`tspi-3m-rk3576-panfrost.dtb` | ARM64实时内核及描述本板硬件的设备树，与`boot.img`内提取的内容一致。 |
| `modules-6.1.99-rt36-tspi.tar.gz` | 与本轮实时内核匹配的155个内核模块及2个附加无线驱动；压缩包保留`modules/`目录结构，恢复位置对应系统的`/usr/lib/`。 |
| `system.config`、`kernel.config`、`manifest_pinned.xml` | 实际系统配置、内核配置及官方源码固定版本。 |
| `environment.txt`、`installed-packages.tsv` | 镜像内的环境说明和软件包版本清单；驱动文件另按模块检查记录核对。 |

Git保存压缩归档及配套文件；展开的`update.img`由`.gitignore`排除，避免把超过单文件限额的重复内容加入版本管理。2026-10-30构建时尚未提交；随后用户于2026-11-02要求清理并提交推送，实际结果统一见[事项记录](../../../02_records/20261030_system_image.md#cleanup-and-push-20261102)。需要从归档重新取得烧录文件时，在Windows项目根目录执行：

```powershell
.\06_programs\rk3576_system\restore_update.ps1 -ArchiveDirectory (Join-Path (Get-Location) '07_backups\system_image\ubuntu24_server')
```

脚本先核对压缩包，解压为临时文件，再核对展开镜像，全部一致后才生成`update.img`。已有同名镜像时只核对，不覆盖；解压中断留下的`update.img.partial`须先核查，再处理。压缩包不能直接交给烧录工具。

## 已预装的环境

| 配置 | 使用方式与用途 |
| --- | --- |
| Ubuntu 24.04.5 LTS Server / ARM64 | 沿用官方Server配置，不包含SN100上位机程序；保留官方硬件和多媒体依赖。 |
| 实时内核`6.1.99-rt36-tspi` | 提供实时调度能力；拿到板子后核对`uname -r`与`cat /sys/kernel/realtime`，后者应为1。实际延迟和业务响应仍需板端测量。 |
| 有线地址`192.168.100.123/24`，网口`end0` | 不绑定旧板MAC，不设置有线默认网关。电脑使用同网段的其他地址；用户确认只接一块板使用该地址。 |
| SSH远程登录 | 开机启动，连接入口`ssh lckfb@192.168.100.123`。新板首次启动自行生成SSH主机密钥。 |
| Samba文件共享 | 开机启动，共享入口`\\192.168.100.123\rkshare`，共享目录`/home/rkshare`，用户主目录`/home/rkshare/lckfb`，程序目录`/home/rkshare/programs`。 |
| 网络管理和时间同步 | NetworkManager管理连接，chrony在具备可用网络时同步时间。 |
| 检查工具 | `cyclictest`查看实时线程唤醒延迟，`stress-ng`制造测试负载；`ip`、`ping`、`ethtool`查看网络；`lsof`、`ps`、`top`查看文件和进程；`gpiod`提供GPIO查看与操作工具，本轮未操作引脚。 |

首次使用共享前，在新板终端或SSH中运行`sudo rk3576-first-setup`，按提示现场设置Samba密码。没有复制旧板的Samba账号库、SSH私人密钥、业务程序或运行数据。

## 本轮验证与后续操作

系统目录和软件包51项检查通过；从实际根文件系统镜像中读取的170个关键文件逐一核对通过，包含全部157个模块。模块经官方相同的去调试信息处理后与编译产物一致，全部带ARM64和实时版本标记。`boot.img`内的内核、设备树及资源文件与编译产物一致；完整镜像已通过官方打包、压缩完整性检查及解压后指纹核对。[详细记录和证据](../../../02_records/20261030_system_image.md)。

拿到同型号新板后，先确认实际连接对象与可覆盖范围，再按[官方整机烧录说明](https://wiki.lckfb.com/zh-hans/tspi-3-rk3576/system-development-compilation/ubuntu24/finished-image.html)在RKDevTool的“升级固件”流程选择本目录`update.img`。烧录后分别登记首次启动、实时标志、固定地址、SSH、Samba读写和需要使用的外设结果；当前这些项目均待板端验证。
