# 原医院资料保存失败：修复与RK复测

本批只收尾医院原库保存的6项失败。按用户在按钮检查窗口转达的要求，完成这一小项后停止，其余原DB/配置接入交新窗口继续。

原实现通过Qt编码医院名称后，又复用同一个临时文件准备保存前回读。Qt的设置写入可能已替换文件，继续复用旧文件会使回读不一致并拒绝保存。现改为另建私有回读副本；原文件的其他键、注释和换行仍保留，原数据库分类、表和未知列不变。检查程序同时补出医院错误原因，便于区分预期拒绝和异常。

| 这次实际执行 | 结果 |
| --- | --- |
| 修复前管理资料检查 | 68项通过、6项失败，实际退出1；6项都在医院保存及后续重开、新增和删除链上。 |
| 修复后ARM64交叉编译 | 成功，退出0；29份源码/头文件/生成文件的指纹在编译前后核对。 |
| 修复后RK匿名原结构检查 | 74项通过、0失败，实际退出0；包含医院保存/关闭重开、134个其他配置键和原字节保持，以及同驱动已有的账户、维护和告警检查。 |
| 当前主程序 | 仍为`f8c6e73f`，未部署本修复、未接入医院原库主页面。 |
| GUI及真实仪器 | 本次没有运行界面或连接仪器；不能把后台通过当成这两项通过。 |

新驱动SHA-256：`a0328adbbfdcb0e08598c6057caf4a222f9377198b217d56b3313e831a81fea5`。源码和构建身份见[verification.json](verification.json)。SHA-256指文件内容指纹，用于确认核对的是同一版本。

| 证据 | 用途 |
| --- | --- |
| [management_01/verification.json](management_01/verification.json)、[原日志](management_01/app.log) | 保留修复前68/6，不以成功结果覆盖旧失败。该批只补存了修复涉及的两份旧源码，不能声称完整恢复了旧驱动全部构建输入。 |
| [management_02/verification.json](management_02/verification.json)、[实际日志](management_02/app.log)、[退出码](management_02/exit_code) | 修复后74/0的逐项结果和实际退出0。 |
| [management_02/final_board_context.txt](management_02/final_board_context.txt) | 北京时间2026-10-27 19:56:17：current未变，没有主窗口或本次驱动进程，应用生命周期锁可取；本用户未见tty7/card0占用，本次未申请显示。 |
| [build/compile.log](build/compile.log)、[输入指纹](build/inputs.SHA256SUMS) | 实际交叉编译和29份输入的身份；[sources/](sources/)保存对应源码快照。 |
| [build/run_data_driver.sh](build/run_data_driver.sh) | 上传到RK私有临时目录，核对驱动及运行组件，只建立匿名原结构，回收真实输出和退出码。 |
| [SHA256SUMS](SHA256SUMS) | 本目录所有其他文件的内容指纹；不含原患者或账户记录。 |

失败和成功两批的板端私有目录均已清理，未将匿名运行数据库或运行时口令复制回电脑。管理驱动编译已结束并释放本窗编译锁；收尾快照里另有原试剂窗口的编译，已注明归属，没有终止它。没有修改现场`runtime/data`或部署指针。

继续工作请先看[同一移植记录](../../../02_records/20261022_sn100_host_migration.md#hospital-save-fix-stop-20261027)与[停止交接](../../../temp/sn100_host/original_db_20261027/integration/stop_handoff_20261027.md)。
