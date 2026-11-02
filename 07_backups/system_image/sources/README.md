# 官方系统源码归档

官方SDK的71个项目已同步，版本固定在`manifest_pinned.xml`。源码文件树和所需大文件已归档为9个分卷，合计9,515,085,096字节；各卷实际内容及按顺序合并后的整体文件指纹均已核对。归档在本地镜像生成调整之前制作，不包含Git对象数据库及后续构建产物。

| 文件 | 用途 |
| --- | --- |
| `official_sdk_36d3aadf_20261030.tar.gz.part001`至`part009` | 官方源码原始文件树的压缩分卷，前8卷每卷1 GiB。 |
| `SHA256SUMS` | 9卷及固定版本清单的文件指纹，用来检查复制或下载是否完整。 |
| `source_archive_parts.json` | 每卷大小、文件指纹及原整包的总大小和文件指纹。 |
| `WHOLE_ARCHIVE_SHA256SUMS` | 合并为原文件名后核对整包时使用，分卷保存状态不直接运行这份清单。 |
| `ubuntu_base/` | SDK选用的Ubuntu 24.04.4 ARM64基础包、Canonical原校验清单及本包校验值。完整系统由SDK继续安装软件包后生成。 |

日常查看或继续修改，直接打开[系统工程](../../../06_programs/rk3576_system/README.md)中的源码入口。下面只用于从归档恢复一份新源码；先在WSL中进入本归档目录，核对分卷：

```bash
sha256sum -c SHA256SUMS
```

选择一个新的Linux目录，避免覆盖现有源码，例如：

```bash
mkdir /home/qyq/rk3576_sdk_restore
```

保持分卷文件名不变，按顺序直接解压，不必额外保存一份9.5 GB的重复整包：

```bash
cat official_sdk_36d3aadf_20261030.tar.gz.part??? | tar -xzpf - -C /home/qyq/rk3576_sdk_restore
```

解压位置需要是WSL的Linux文件系统，保留符号链接和权限；不要把解压后的源码直接放在普通Windows目录编译。实际工程当前已完整保留Git工作区和构建数据，分卷归档则用于保存原始文件和恢复。
