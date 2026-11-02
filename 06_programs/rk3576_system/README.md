# RK3576完整系统工程

这里用于给立创泰山派3M RK3576编译Ubuntu 24.04 Server整机镜像，并预先配置实时内核、固定地址、SSH远程登录、Samba文件共享、用户主目录和必要检查工具。完整编译与最终打包已通过，结果为Ubuntu 24.04.5 LTS Server、`6.1.99-rt36-tspi`实时内核；系统内容51项和实际镜像文件170项核对均通过。[完整镜像与校验清单](../../07_backups/system_image/ubuntu24_server/README.md)已正式保存。板子不在身边，本轮未烧录，板端启动与服务功能待验证。

| 文件或位置 | 用途 |
| --- | --- |
| `build.sh` | 在专用WSL中用普通用户编译完整系统，接收一个绝对的输出文件路径。已有同名输出时停止，避免覆盖。 |
| `prepare_rt_kernel.sh` | 核对原内核提交与补丁指纹，接入此前已在原板验证的实时补丁和配置片段。 |
| `prepare_image_sources.py` | 在原始源码已归档后，为镜像增加首次启动准备：清除编译时生成的SSH主机密钥、机器标识和随机种子，由每块新板分别生成。不会覆盖已有的其他源码修改。 |
| `rootfs/setup-environment.sh` | 在构建新系统时安装和配置SSH/Samba、网络、时间、共享目录和检查工具。 |
| `rootfs/rk3576-first-setup` | 新板首次使用时现场设置Samba密码的入口；构建时不执行，不写入通用密码。 |
| `rootfs/ssh-keygen-firstboot.service` | 新板启动时生成缺失的SSH主机密钥，在远程登录服务启动前完成。 |
| `rootfs/post-rk3576-environment.sh` | 在官方最后一轮目录归属处理之后，恢复共享目录及其中用户文件的归属和权限。 |
| `finish_image.sh` | 核对首次启动服务；如需修正则重新打包，再执行系统文件检查。 |
| `verify_rootfs.py` | 在电脑上核对生成系统的包、目录权限、网络、服务及模块文件指纹，不启动板端服务。 |
| `verify_image_files.py` | 只读访问实际根文件系统镜像，逐文件核对关键配置、程序和全部157个模块。 |
| `restore_update.ps1` | 在Windows中从正式压缩归档恢复可烧录的`update.img`，先后核对压缩包和解压文件的指纹。 |
| `configs/` | 当前实时内核与系统配置；暂停时的旧配置另存本次资料目录。 |
| `patches/ubuntu_identity.patch` | 本轮镜像首次启动准备的实际源码差异。 |
| `wsl_workspace/` | 已保存专用WSL的工作磁盘，里面包含实际系统源码和构建数据。它是可继续使用的工作环境，不能随意清理。 |
| `open_system_sources.lnk` | 双击后在资源管理器中打开实际编译用的系统源码。 |
| [官方源码原始归档](../../07_backups/system_image/sources/README.md) | 已保存官方源码的9个分卷、固定版本清单及文件指纹。 |
| `07_backups/system_image/ubuntu24_server/` | 已保存完整`update.img`及压缩归档、匹配模块、启动文件、配置和恢复说明。 |

实际源码需要Linux文件系统的权限、设备文件和符号链接，因此放在专用WSL的Linux磁盘内；源码目录为`/home/qyq/TaishanPi-3-Linux`。Windows资源管理器也可直接打开`\\wsl.localhost\RK3576-Build-22.04\home\qyq\TaishanPi-3-Linux`。工作磁盘已从临时区域迁移到本目录的`wsl_workspace/`。大工作磁盘不直接纳入Git；正式源码原始归档、版本清单、配置和最终镜像按事项保存。

构建入口沿用官方Server配置，增加实时内核片段；默认使用CPU0至CPU7，并在生成配置中选择Ubuntu国内镜像站。可通过`RK_SYSTEM_CPUSET`指定其他CPU范围，通过`RK_SYSTEM_UBUNTU_MIRROR`指定Ubuntu软件源主机名。实际编译结果以[操作记录](../../02_records/20261030_system_image.md)为准。

镜像生成脚本另外保留上述首次启动准备的差异；官方原始归档在这些调整之前制作。这样不会把本次编译生成的SSH私钥复制给以后每块板子。首次启动和SSH实际连接仍需烧录后验证。

镜像目标配置：`end0`使用`192.168.100.123/24`，不设置有线默认网关；共享入口为`\\192.168.100.123\rkshare`，目录`/home/rkshare`，账号`lckfb`，主目录`/home/rkshare/lckfb`。用户确认只连接一块板使用该地址。实际共享登录前在新板执行`sudo rk3576-first-setup`，现场设置共享密码；当前尚未上板验证。

后续需要重新生成时，在Windows项目根目录执行下列命令，输出文件必须是新的绝对路径。入口依次核对RT补丁、接入环境脚本、执行官方完整编译、检查并打包首次启动准备、核对实际镜像文件，全部通过后才复制完整输出。

```powershell
wsl.exe -d RK3576-Build-22.04 -u qyq -- bash /mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/rk3576_system/build.sh /mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/system_image_next/update.img
```

本轮最终镜像来自完整构建之后的目录归属修正及重新打包。`temp/system_image_20261030/build_candidate/update.img`的首轮候选文件已于2026-11-02清理；正式目录中的最终镜像保留，最终SHA-256以正式目录`image_manifest.json`为准。

后续构建的检查报告默认放在新输出目录的`checks/`内，保留本轮正式证据；需要另选位置时设置绝对路径`RK_SYSTEM_EVIDENCE_DIR`。

2026-11-02已清理内核和附加无线驱动的可重建编译对象、命令缓存及内部RKAF打包文件，并清理Go编译缓存。源码、内核配置、匹配模块、WSL中的最终`rockdev/update.img`、已配置系统目录及根文件系统镜像均保留；下次完整构建会重新生成已清理的对象。[清理范围与证据](../../02_records/20261030_system_image.md#cleanup-and-push-20261102)。
