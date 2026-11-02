# 新板整机镜像与编译输入

已为立创泰山派3M RK3576生成Ubuntu 24.04.5 LTS Server整机镜像，预装实时内核、SSH远程登录、Samba文件共享和固定地址。完整编译、系统内容检查、实际镜像文件核对及压缩归档解压恢复均通过，可烧录的`update.img`已保存。板子不在身边，本轮未烧录，板端启动和服务待验证。

| 位置 | 用途 |
| --- | --- |
| `build_host/ubuntu-22.04.5-wsl-amd64.wsl` | Canonical发布的电脑端WSL编译环境基础包，不是RK系统镜像。 |
| `build_host/ubuntu22_SHA256SUMS` | Canonical原发布校验清单，本次已核对WSL包对应的SHA-256。 |
| `build_host/go1.24.13.linux-amd64.tar.gz` | 官方设备树工具所需的电脑端Go编译器安装包，已与官方发布指纹核对。 |
| [sources/](sources/README.md) | 官方SDK的9个压缩分卷、固定版本清单、文件指纹及Ubuntu ARM64基础包；附恢复方法。 |
| [ubuntu24_server/](ubuntu24_server/README.md) | 可直接烧录的`update.img`、同内容压缩归档、匹配启动文件、模块、配置与校验清单；附首次使用方法及待板端验证范围。 |

实际系统源码及构建入口在[06_programs/rk3576_system](../../06_programs/rk3576_system/README.md)，工作磁盘已经移入该工程，原源码并非只保留在临时目录。

本轮按用户最新要求只下载、编译和归档，后续烧录另安排。实际过程、来源和当前完成范围见[同一事项记录](../../02_records/20261030_system_image.md)。二进制安装包及最终镜像使用Git LFS；不自动提交或推送。
