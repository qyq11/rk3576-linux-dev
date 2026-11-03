# 用户更正后的SN500主控：替换、改名和实时日志

2026-11-02，用户最终指定`ACA_V1(1).0OffLineD-500`替代前一份发错的`ACA_V1.0D-500`，要求去掉`(1)`，并确认由本聊天启动、实时显示日志。本轮按最终文件执行，前一轮失败仍在`../replace_04/`保留，不能当作当前程序结果。

板B硬件序列号`8314b1e8ffd0321f`，SSH指纹匹配`SHA256:S4ihaw0V/Z8w7v3VlJOQEFLEH3Hp6nr3KjiUTxanwPg`。当前程序为`/userdata/shared/ACA_V1.0OffLineD-500`，10,131,952字节，文件指纹`ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff`，所有者root、权限755。板端旧的OffLine程序及随后误发的D程序均已从活动运行位置删除，旧测试/恢复归档保留原样；配置和流程目录在替换时逐文件核对未变。

17:38:07实际以root运行，日志报告编译时间2026-11-02 17:35:14，主进程PID39408，进程标题为原程序自行设置的`SN450_CPU3`；这个标题不能作为实际机型判断依据。实际Qj返回机型SN500、匹配号20250905016、LinuxNew标识、普通/急诊各5孔、24通道，见[只读查询](../connection_probe_06/)。5000端口已监听，进程持续运行；保持运行时没有退出码，不能记为退出0。

日志窗口标题为`RK3576 Board B - Correct ACA live log`，直接通过SSH前台显示原始输出，同时保存到`/home/rkshare/programs/aca_flow_test/runs/correct_05/live_20261102_173807/program.log`。本目录`program_snapshot.log`是取证时的固定副本；`live_20261102_173807/program.log`也是复制时的快照，不会继续同步。

## 后续怎样启动和看日志

当前程序保持运行，不要再启动第二份。以后停止后，在板B的root终端运行：

```bash
cd /userdata/shared
LD_LIBRARY_PATH=/userdata/shared stdbuf -oL -eL ./ACA_V1.0OffLineD-500
```

需要同时存日志且保存真实退出码时，可用本轮脚本：

```bash
bash /home/rkshare/programs/aca_flow_test/run_correct_live.sh
```

脚本先核板B身份、文件指纹和是否已有同名进程，然后显示并保存日志；终端中按Ctrl+C结束。另开终端只查看现有日志可用：

```bash
tail -f /home/rkshare/programs/aca_flow_test/runs/correct_05/live_20261102_173807/program.log
```

## 通过范围和仍有的问题

本轮完成新文件指纹/架构/运行组件核对、替换、755权限、实际启动、持续日志及5000监听。读取配置和试剂库的实际日志已保留。主控的CAN、串口等打印为脱机模拟，未据日志中的“send”认定排针真实收发。

启动时还打印`ReagentCalibrationFile is load failed!`，两份流程CSV的空文件/表头读取错误，以及早期GPIO15目录不存在和eth0名称不匹配；原包两份流程CSV本来就是0字节，尚未补造内容。程序仍继续运行。配置/数据库在运行中由程序打开后，`project/1 PT.db-shm`指纹发生变化，这是实际观测，不把替换前后的一致检查扩大为运行期间所有文件始终不变；原因未进一步认定。

未发送开始、暂停、任务或机械动作命令，未验证真实仪器及完整检测流程；后续上位机连接验收另在同一事项中追加。

| 文件 | 核对用途 |
| --- | --- |
| [result.txt](result.txt)、[before.log](before.log)、[dependencies.log](dependencies.log) | 替换对象、最终指纹、权限和运行组件 |
| [support_files_before.sha256](support_files_before.sha256)、[support_files_after.log](support_files_after.log) | 仅替换可执行文件时的配套资料一致性 |
| [support_files_during_run.log](support_files_during_run.log) | 程序运行后共享内存配套文件的变化 |
| [running_snapshot.log](running_snapshot.log)、[program_snapshot.log](program_snapshot.log) | 取证时主控进程、监听端口及原始输出 |
| [run_correct_live.sh](run_correct_live.sh)、[show_correct_live_log.ps1](show_correct_live_log.ps1) | 本轮实时显示与保存方法 |

电脑当前有效输入在`05_tools/tests/sn100_host/current/`，使用无括号的文件名；原微信附件保留原样。此前电脑旧原文件的删除被自动审批以`blocked by policy`拦截，未换其他方式绕过；这不影响板端活动文件已替换。正式恢复程序见[正确程序归档](../../../../../07_backups/sn100_host/board_b/aca_flow_test_20261102/correct_program_1735/README.md)。
