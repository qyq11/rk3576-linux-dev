# 板B测试程序首次部署与启动

**当前主控已启动：**用户最终更正为`ACA_V1(1).0OffLineD-500`后，按要求去掉`(1)`，部署至`/userdata/shared/ACA_V1.0OffLineD-500`、设755；17:38:07起持续运行并监听5000，实时日志窗口保留。[正确文件的本轮证据](correct_05/README.md)与[只读Qj收发](connection_probe_06/)单独保存。下文key.ini失败均为此前文件的历史结果，不作为当前文件结论。

**当前补充（17:23:26）：**按用户提供的绝对路径截图，已将原包43文件放入板B实际目录`/userdata/shared/`并保留；配置可读，部署及运行后原文件内容全部一致。程序仍在读取key.ini失败后退出255，尚未打开config_linux.ini；下一步核对应入口源码和编译设置。[实际固定目录部署](fixed_path_03/README.md)。下文保留首轮范围及原证据。

2026-11-02，用户要求部署`ACA_V1.0OffLineD-500`并连接上位机。**本目录首轮文件部署及运行库检查通过；运行中读取`/etc/init.d/key.ini`失败后退出255，尚未开放5000端口。**后续用户转达作者说明无需该文件；此前直接要求补key.ini的判断撤回。[完整包直接复测](recheck_full_package_02/README.md)仍复现退出，当前待核对应程序版本、启动条件或入口检查代码。

板B序列号`8314b1e8ffd0321f`，SSH指纹`SHA256:S4ihaw0V/Z8w7v3VlJOQEFLEH3Hp6nr3KjiUTxanwPg`；部署到`/home/rkshare/programs/aca_flow_test/`。原件及板端文件SHA-256均为`bfb23e400584ee9f3b44eee2680fdd8eff85c51729963510f8612c71926ae33f`，大小10,256,176字节。没有重新编译。

| 文件 | 怎样核对 |
| --- | --- |
| [probe_01/program.log](probe_01/program.log) | 板B程序原始输出，含编译时间、缺配置错误及报错位置。 |
| [probe_01/dependencies.log](probe_01/dependencies.log) | 运行库逐项位置，没有缺库。 |
| [probe_01/listeners_before.log](probe_01/listeners_before.log) | 启动前的监听端口。 |
| [probe_01.log](probe_01.log) | 上传指纹、临时服务首次状态与程序实际退出255、启动后端口检查。 |
| [probe_01_ssh_exit.txt](probe_01_ssh_exit.txt) | 外层脚本因管道末尾换行返回127，与程序退出255分别保留。 |
| [deploy_probe.sh](deploy_probe.sh) | 首轮实际脚本；先核板B身份和文件指纹，普通用户、独立数据、限制设备与系统写入，最多30秒。复用时先上传脚本再执行，并使用新的运行目录/服务名。 |
| [binary_flags.json](binary_flags.json) | 只读提取的串口/CAN等模拟标记初值，不代替启动后的行为验证。 |
| [inspect_binary.py](inspect_binary.py) | 电脑端只读解析脚本，不是板端测试程序。 |
| [state_after_probe.log](state_after_probe.log) | 首轮之后服务已卸载，默认状态0不作为通过证据。 |
| [final_check.log](final_check.log) | 测试程序最终进程数0、指纹及空挂载目标清理；上位机新一轮仍是原脱机模式。正在运行时不存在result.txt，相关cat错误不代表应用崩溃。 |

本次只新增测试目录，未修改上位机数据、启动器或连接配置，没有接真实仪器。临时映射产生的空`/userdata/shared`已移除，原`/userdata`内容保留。程序还引用其他流程资料，是否必需应以本版本配套包核对；不凭静态字符串补造参数。

完整过程追加[原移植记录](../../../../02_records/20261022_sn100_host_migration.md#test-files-20261102)，继续顺序见[测试目录说明](../../../../05_tools/tests/sn100_host/README.md)，程序恢复输入见[正式归档](../../../../07_backups/sn100_host/board_b/aca_flow_test_20261102/README.md)。尚未核功能、具体收发、资源预算或长时间稳定性。

`SHA256SUMS`保存本目录除README外的文件指纹；在本目录用`sha256sum -c SHA256SUMS`可核原始内容。
