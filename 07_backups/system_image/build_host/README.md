# 电脑端编译环境安装输入

这里保存Windows电脑中专用WSL编译环境的基础安装包，不能烧录到RK板。实际使用的环境名是`RK3576-Build-22.04`，系统为Ubuntu 22.04，普通用户为`qyq`；它生成的板端系统仍是Ubuntu 24.04 Server / ARM64。

| 文件 | 用途与来源 |
| --- | --- |
| `ubuntu-22.04.5-wsl-amd64.wsl` | Canonical发布的Ubuntu 22.04.5 WSL基础包；[官方发布目录](https://releases.ubuntu.com/22.04/)。本次通过中国科学技术大学镜像站取得，与官方指纹一致。 |
| `ubuntu22_SHA256SUMS` | Canonical原发布校验清单，保留原样。 |
| `go1.24.13.linux-amd64.tar.gz` | 电脑端Go编译器，用来交叉编译官方设备树工具`ovtree`。该工具要求Go 1.24；[官方发布信息](https://go.dev/dl/)。本次经镜像站取得，与官方指纹一致。 |
| `go_download.json` | 本次使用的Go官方发布信息，含架构、大小和文件指纹。 |
| `SHA256SUMS` | 本目录两个安装包的SHA-256文件指纹，用来核对复制或恢复后的内容。 |

在WSL中进入本目录后运行`sha256sum -c SHA256SUMS`检查完整性。专用环境导入、依赖安装和Go准备的实际过程见[操作记录](../../../02_records/20261030_system_image.md)，原依赖准备脚本见[prepare_build_host.sh](../../../04_references/system_image/20261030/prepare_build_host.sh)。准备脚本针对新建编译环境，不用于改造已有的日常WSL。

正在使用的系统源码和构建数据在专用WSL的Linux工作磁盘内，磁盘现位于`D:\WSL\RK3576-Build-22.04\ext4.vhdx`，操作入口见[系统工程](../../../06_programs/rk3576_system/README.md)。本目录保存的是可重新安装的输入，不包含密码或私人密钥。
