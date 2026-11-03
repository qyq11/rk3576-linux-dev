# 板B改用原Windows配置与配套资料

**18:13:32后续变更：用户要求删除旧脱机资料并停止后续脱机测试。**旧`runtime/previous_test_data_20261102`已删除，当前原资料保持不变；下文“保留旧资料、切回”的描述属于删除前安排，现已不适用。历史恢复脚本和固定证据保留原字节，不再用它们恢复旧测试资料。[实际删除及核对结果](../20261102_remove_offline_data/README.md)。

2026-11-02，用户明确要求日常上机不再使用独立测试资料，完整沿用原Windows `bin` 的配置、数据库和资源；同时确认以新提供的SN500主题INI/截图为准，并授权与“配置 Linux 主控资料路径”聊天协调。这里保存文件部署和只读检查证据，程序发布与主控连接结果另行引用，不能把文件检查当作检测通过。

## 当前文件放在哪里

板B序列号`8314b1e8ffd0321f`，严格校验SSH主机指纹`SHA256:S4ihaw0V/Z8w7v3VlJOQEFLEH3Hp6nr3KjiUTxanwPg`后操作。原Windows参考文件保持只读；141份配套资料放入板B`/home/rkshare/programs/sn100_shell/runtime/data`，保留原分类、文件名和大小写。29份原库没有转成其他库或改表。

Windows资源管理器地址栏使用`\\192.168.100.123\rkshare\programs\sn100_shell\runtime\data`。这里的`config.ini`、`config_Debug.ini`、`theme_config.ini`及`calibrationFile/ReagentAmountCalibration.ini`是当前原配置工作副本；修改前正常关闭程序，只备份要修改的文件，保存后重开并核实际读取结果。

原测试数据整体从`runtime/data`移到同板`runtime/previous_test_data_20261102`，供需要时切回，不再用作当前原资料根。`original_project_root`记录新原资料根；日常`sn100.sh`检测此指针后不再追加`offlineAdmin`匿名会话，原登录开关和账户不改。

## 哪些参数改了

10月27日历史记录中四份原INI字节未改，兼容处理发生在读取副本。本次以那批141份原资料为来源，仅替换/修改以下两份INI，其他139份字节保持一致。逐键依据见`configuration_changes.json`。

| 文件与键 | 原值 | 本次值 | 原因 |
| --- | --- | --- | --- |
| `theme_config.ini`：`theme/DriverModel` | SN450 | SN500 | 用户新INI/截图；旧程序中的SN100默认文字不作为本次配置依据 |
| 同上：`theme/ReagentPanelChannelCount` | 20 | 42 | 新INI |
| 同上：`theme/SampleHoleCount` | 10 | 5 | 新INI |
| 同上：`theme/EmergencyHoleCount` | 3 | 5 | 新INI |
| 同上：`theme/ChannelCount` | 18 | 24 | 新INI |
| `config.ini`：`tcpmain/ip` | 192.168.100.123 | 127.0.0.1 | 当前主控与上位机同在板B运行 |
| 同上：`tcplog/ip` | 192.168.100.123 | 127.0.0.1 | 同板连接交接要求 |

主通信端口原本就是5000，报告方式`theme/ReportTableType`原本就是1，本次保持。新主题全文与用户附件一致，指纹`029605f35924de646b5402f1e377c93cb26e01952e0daa5fda300fe47f7feed7`；颜色、名称、公司及标志尺寸原文保留。`config_Debug.ini`的原登录设置及其余配置不改。源数据库通道表只读盘点为25行、24行启用，原表保留；盘点只读配置，不查询患者或账户记录。

## 在RK核对到了哪里

- 安装结果`install_result.txt`：141份文件、29份数据库，实际上传包及解包后文件指纹一致；原测试目录已保留。安装时主程序未运行，未启动或停止主控。
- `files_check_result.json`：复用正式`original_files/check.cpp`及`original_ini.cpp`交叉编译ARM64检查器，在板B普通用户lckfb下实际运行。141文件的指纹和格式均通过，29库只读打开且`PRAGMA quick_check`通过；四份INI分别34、42、32、26项，共134项，读取值及类型与对应配置原文一致。
- `files_check_exit.txt`及`check_ssh_exit.txt`均为0；失败0、兼容告警0。检查后再次核141份文件指纹，全相同。检查器不连接主控、不查询或输出患者/账户记录；临时匿名INI仅用于核注释、编码和文字解析。
- `channel_configuration_inventory.json`是电脑对配置源的只读数量盘点，不是电脑运行功能或界面测试。
- `windows_share_config_read.json`记录Windows通过root共享实际读到四份配置及其指纹，未改文件；`launcher_status_check.log`记录板B普通用户实际执行日常状态入口、原资料指针和标记，状态检查退出0，当时主程序关闭。
- 当前正式程序读取、正常退出重开、SN500主题数量及主控收发：正在由主控资料聊天更新程序并验收，本次尚不据文件检查宣布通过。真实仪器、完整开始/暂停流程、所有参数实际作用、资源预算及长稳仍须分别验收。

## 证据与切回范围

`effective_manifest.json`和`FILES.sha256`记录141份工作文件；`package_identity.json`记录实际输入包。`.sh`、`.py`保留部署和准备过程；`sn100.sh.before`是旧启动入口，不含认证资料。`checker_sha256.txt`对应实际运行的检查器，依赖检查见`check_dependencies.log`。原始证据不包含数据库内容或认证INI内容。

[有限恢复输入](../../../../07_backups/sn100_host/board_b/original_files_20261102/README.md)保留启动器前后版本、恢复脚本、用户新主题和改动清单。旧测试资料只在板B就地保留，不把账户或患者资料打包带入Git；切回依赖该保留目录。恢复脚本在出现后续主题/连接指针时会停止，须先核后续改动，不能撤销另一项工作。

实际过程统一追加[移植记录](../../../../02_records/20261022_sn100_host_migration.md#board-b-original-files-20261102)，当前待办维护在[同一计划](../../../../01_docs/plans/sn100_host_migration_plan.md#board-b-deployment-20261102)。`SHA256SUMS`核对本目录说明以外的证据。
