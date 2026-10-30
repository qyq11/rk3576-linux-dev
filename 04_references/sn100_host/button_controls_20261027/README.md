# 原版检测任务报文的RK检查

这批证据说明独立报文组件及匿名原结构数据库后台符合本次用例。没有连接真实仪器，没有读写生产数据库，没有接通正式主界面的开始检测，也没有计算检测结果。

| 程序 | RK结果 | 实际范围 |
| --- | --- | --- |
| `original_task_codec_rk_test` | 6通过、0失败，退出0 | 完整/设备LinuxNew两种原版任务金样、延长检测、空架号与空任务回包、非法字段拒绝 |
| `original_result_codec_rk_test` | 5通过、0失败，退出0 | 原TD两种进度格式、Tr任务身份与全部曲线采样、缺字段和非法数据拒绝 |
| `original_detection_rk_test` | 10通过、0失败，退出0 | 原组合、编号/报告行、试剂扣量、重复请求、扫码选架、Ur写回、TD/Tr原曲线落盘及写失败共同回滚；仅匿名原结构库 |
| `original_task_transport_rk_test` | 3通过、0失败，退出0 | RK本机TCP分包后完整收到一次128000字节任务报文，无解码错误；没有连接外部仪器 |
| `original_project_codec_rk_test` | 6通过、0失败，退出0 | 原Up发送/UP返回各自字段顺序、针名转换、金样和错误输入保护；尚未做实际项目库与设备同步 |

统计包含测试初始化和收尾。新版任务证据为`staged_task_02/`，替代旧任务驱动的当前结论；旧任务证据继续保留原范围。结果解析源未改，沿用原`staged_result_01/`实际RK结果，不重复冒充新测。

由“SN100项目配置：RK整合验证”窗口运行，RK为ARM64/Ubuntu24.04/Qt与QtTest5.15.13。匿名驱动使用独立进程，没有占用显示或改变当前正式程序。逐文件指纹、实际退出码及范围见[核对清单](verification.json)，原日志保持采集时内容。

对应独立类和测试已保存到正式`samples/`、`reagents/`、`original_files/`和`projects/`；没有改正式main、页面、worker或工程清单。暂存页面/worker/shell等25份副本编译生成18个ARM64对象通过，但没有链接或运行该主程序。`source_snapshot/`保留本次待整合副本及测试源码，不作为另一份维护工程；修改继续在正式源码或当前暂存工作中进行，后续另存新批次证据。`driver_build_04.log`保留成功后台及随后头文件路径失败，`05`保留中断，`06`为后两驱动成功；对象日志另存，没有部署失败或旧产物。

业务入口/后续验收见[唯一移植记录](../../../02_records/20261022_sn100_host_migration.md#original-task-wire-20261027)与[当前计划](../../../01_docs/plans/sn100_host_migration_plan.md#original-database-reuse-20261027)。
