# Linux编译环境迁移到D盘：核对证据

2026-11-02将`RK3576-Build-22.04`的工作磁盘从项目`06_programs/rk3576_system/wsl_workspace/`迁到`D:\WSL\RK3576-Build-22.04\ext4.vhdx`。使用WSL管理功能，名称、用户设置和Linux内部路径保留。完整过程维护在[同一镜像记录](../../../02_records/20261030_system_image.md#wsl-move-d-20261102)。

| 文件 | 核对用途 |
| --- | --- |
| `disk_before.json`、`disk_move_result.json` | 停机磁盘与迁移后首次启动前的整盘大小、文件指纹、WSL登记位置与默认用户设置；两次指纹一致，原位置已无磁盘文件。 |
| `environment_before.json`、`environment_after.json` | `qyq`身份、Ubuntu版本、10个关键文件、5个源码仓库状态、7项工具版本及项目访问结果；前后完全一致。 |
| `verification_result.json` | 原SDK实际完成C/C++的ARM64编译链接、设备树编译；产物架构和头部核对通过。 |
| `windows_access.json` | D盘新位置、原位置、Windows源码快捷方式及C盘项目访问核对。原位置保留一个空文件夹。 |
| `migration_first_attempt.log`、`migration_execution.log`、`wsl_move.log`、`verification_execution.log` | 首次句柄占用、最终迁移成功、启动及编译核对的原始输出；保留原字节。 |
| `inspect_environment.py`、`verify_environment.py`、`move_environment.ps1` | 当次脚本原件，仅供审计；原执行位置在项目temp内，不从本证据目录再次执行迁移脚本。 |
| `migration_probe.c`、`migration_probe.cpp`、`migration_probe.dts` | 编译检查使用的小型输入；程序没有执行，小配置没有应用到板子。 |
| `SHA256SUMS` | 归档原件的文件指纹；本说明单独维护。 |

整盘指纹只对应本次停机与首次启动前的时刻；正常启动写入后可能变化。日常工作继续使用`wsl.exe -d RK3576-Build-22.04 -u qyq`及原源码入口，SDK仍为`/home/qyq/TaishanPi-3-Linux`。

本次核对电脑端编译环境，没有重新生成完整系统镜像，没有连接或改写RK板，不新增板端或真实硬件通过结论。正式源码原始归档和整机镜像继续放在项目`07_backups/system_image/`。
