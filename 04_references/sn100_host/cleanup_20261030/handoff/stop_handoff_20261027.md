# 原DB与配置接入窗口：收尾后停止交接

更新时间：2026-10-27，北京时间。用户在“SN100：RK全部按钮可用性检查”窗口明确要求两个旧窗口只完成最近一项，然后停止，交新窗口继续；该窗口已授权转达。本窗最终小项为“医院原库保存6项失败的修复及针对性RK复测”，现已完成，**停止自动接续，不再推进全部业务整合或发布**。

## 已完成的小项及证据

修复`06_programs/sn100_host/data/hospital_store.cpp`的医院名称保存前回读：Qt编码名称后不再复用原临时文件，而使用新建私有副本核对；`tests/original_management_rk_driver.cpp`补医院错误输出。原`db/userSet/hospital_Information.db`、根`config.ini`和`theme_config.ini`组织不变，不新增医院替代库。

- 修复前驱动`03365e6a...`，RK实际68通过/6失败、退出1；失败资料及两份对应旧源码已保留。
- 修复后完整独立驱动ARM64编译成功、退出0，29份输入与冻结指纹一致。
- 新驱动SHA-256为`a0328adbbfdcb0e08598c6057caf4a222f9377198b217d56b3313e831a81fea5`，RK实际74通过/0失败、退出0。
- 覆盖医院名称/字典保存、正常关闭重开、新增可空助记符/备注和删除，原134个其他键的类型和值、其他原字节及未知列保留。同驱动另含已有账户、维护和告警检查，不能把74称为74项医院完整功能。
- 只有独立后台匿名原结构验证；没有正式主页面、物理输入、视觉、性能预算或真实仪器通过。

长期证据：[hospital_save_fix_20261027](../../../../04_references/sn100_host/hospital_save_fix_20261027/README.md)。修复后医院源码SHA为`a40f57ab20956e033279b8e5d091266b05baf237c7affb50819456fc290fdd72`，驱动源码SHA为`dc0f00110eca4936bc7e18f6ad462e23b9fa3ad124fe9db1a53c11dcf0cc61ba`。全部源码和工具指纹见该目录的`verification.json`及`build/inputs.SHA256SUMS`。

实际使用的命令如下，从WSL项目根执行。检查编号目录必须是新的，脚本会拒绝覆盖旧证据；不要盲目重跑管理02。

```bash
bash temp/sn100_host/all_original_files_20261027/run_original_core_driver_build.sh management
bash temp/sn100_host/all_original_files_20261027/run_data_driver.sh management_02 management \
  "$PWD/temp/sn100_host/all_original_files_20261027/management_build/original_management_rk_driver" \
  a0328adbbfdcb0e08598c6057caf4a222f9377198b217d56b3313e831a81fea5
```

实际末行：`MANAGEMENT_ORIGINAL_RESULT 74 0`、`ACTUAL_EXIT_CODE=0`、`RK_DATA_DRIVER_PASSED=management_02`、`PRIVATE_TEMP_REMOVED=1`。二进制仍在`temp/sn100_host/all_original_files_20261027/management_build/`，没有替换正式主程序。旧`management_driver_handoff.json`是修复前03365批次，不能继续当新产物身份；新身份以上述长期证据为准。

## 当前RK、进程和锁

2026-10-27 19:56:17（北京时间）只读实查：

- `current`为`/home/rkshare/programs/sn100_shell/releases/f8c6e73f59f458981e243af5dd00999c12a671f449f0daaf3f4ff70e82d8a11f`。
- 主程序`/home/rkshare/programs/sn100_shell/current/sn100_shell`SHA为`07ae71ce7dc2d484f9eba8c4a993c5c8768c6a232fbbbd0c0bc0b286464582c2`；报告组件SHA为`2a054d8caf9bd76bea4fff5eac45d5611579f4df79ab76adc2981a89af6bedcc`。
- 没有`sn100_shell`主窗口或本次数据驱动进程，lifecycle锁可取；本用户未见`/dev/tty7`或`/dev/dri/card0`被打开。本次没有申请或占用显示，不能把本用户检查扩大成所有用户均无显示进程。
- 管理01失败目录和管理02成功目录均已删除，只删除核过路径、驱动指纹和退出码的本次私有目录。没有改现场runtime、部署指针、原库或系统设置，没有连接真实仪器。
- 本窗没有仍运行的编译、检查、子Agent或排队GUI。19:56快照另有**原试剂窗口**的`single_reagent_update_objects`编译，不属于本窗，没有终止；该窗口随后已回报编译结束并停止，交接见`temp/sn100_host/button_controls_20261027/stop_handoff_20261027.md`，新驱动`2852eddf...`未在RK运行。
- 本地自动化目录未找到包含当前窗口ID`01a17b4b-f25b-7913-9345-326ce1bda114`的自动化配置；本窗没有再创建监测或派发下一批工作。

## 更早完成的文件清单与已收回结果

[Windows/RK逐文件清单](../../../../01_docs/knowledge/sn100_original_files_rk_checklist.md)实查时间19:19:23，29DB+9配置/规则+1报表XML共39份在RK全存在、内容一致、缺失0。每行两边完整路径，原分类/文件名/大小写保持；3个原空目录未创建，已单列。**这证明文件齐全，不证明业务全部接入。**RK数据根是`/home/rkshare/programs/sn100_shell/runtime/data`；正式仍为f8c，所以该清单的部署版使用阶段未因独立后台通过而改变。

停止要求到达前已执行的独立RK检查：project-codec 6/0、project-device 7/0、device-sync 6/0，均退出0，已由原试剂窗口归档到`04_references/sn100_host/original_flow_20261027/`。质控/校准旧驱动`cf626fce...`实际29/0退出0，资料在`temp/sn100_host/all_original_files_20261027/qc_calibration_01/`；对应的是该二进制和既有冻结输入，不外推为当前整主程序通过。原试剂GUI`ff9b21ea...`没有在本窗执行，新窗口应登记待RK验证，不沿用旧页面证据。

## 已有半成品，原样保留供新窗口继续

下述不是本次医院小项完成项，也没有因停止而回滚。工作区本来已有大量未提交修改，别用checkout/reset/clean覆盖。逐文件原始状态及当前指纹另存本目录`stop_working_files_20261027.json`；本窗未自动提交或推送。

- `shell/main.cpp`仍与f8c发布源码同SHA：`2db5d584932531369d705f72c85b85f955ca78022b41d41a13706108b58db43c`。没有完成原医院/账户/维护/告警、历史、试剂/样本、质控/校准、打印/LIS的统一初始化与主页面接线；当前正式仍大量使用独立子目录。
- `original_files/original_ini_write.*`提供保留其他字节的原INI写回；`original_ini.*`补大小写不敏感查键。新INI读取代码随管理驱动编译并用于上述医院检查，但通用patch函数没有在该驱动使用，不能当写回全部通过。
- `data/printer_settings_store.*`已有`initializeOriginal`，`data/lis_settings_store.*`本窗在收到停止要求前已补读取原键、保存当前模式、保留底层参数与回读。**这些改动尚未编译或RK测试**，需优先检查原枚举、未知字段、保存/取消、跨页同时改根config.ini和只保存当前通讯方式的语义。不要按“已写代码”标成功。
- `original_runtime_settings.*`新的通信显示过滤改为`showCommunicationCommand(command, bool sending=false)`，发送/接收分别处理、show优先mask、保留空token；改后未重新执行其专项RK驱动。旧runtime_02的46/0不能覆盖这些新行。原主题Logosize已有非默认20x20读取证据，但完整主题尚未接入；`shell/frmmain.h`有applyOriginalRuntime声明、CusMenuButton有颜色接口，`shell/original_runtime_shell.cpp`还没有实现文件。
- 原管理资料的四个Store已能在独立驱动使用；账户来源、Configured会话、权限字典和role0规则要按原Windows接入，不能用offlineAdmin模拟替代原账户。医院保存与其他页面共享根config.ini冲突处理仍需统一验证。
- `history/original_history_source.*`和HistoryStore原模式已有独立RK9/0，未接主入口。原外部`resource/report_template/report.xml`已复制，但正式仍用字节不同的内嵌模板，不能说原外部模板完成。
- `quality_control/qc_original_store.cpp`、`calibration/calibration_original_store.cpp`及原模式API已写；旧驱动29/0已收回。停止前静态审计发现QC页面会校验未改的旧规则、校准把power1套到过多写操作，以及方法设置弹窗仍是临时布局；**这些新发现没有修复**。须重新只读对照原Windows逐操作权限再改，不能把29/0当全部页面一致。
- 原试剂/样本和任务链由另窗维护，最新单孔清除/Us15字段、role0、样本状态图及主题setter在该窗收尾；以其stop_handoff和新编译身份为准。`shell/device_control_shell.cpp`本次没有接完整准备/同步/收包，不能提前称正式开始检测通过。
- 新后台和需要的源文件尚未统一加入shell的`.pri`及`build.sh`，公共AccountSession、头文件和moc发生过变化；新窗口整合时必须按一致的新输入完整交叉编译，不能混入旧对象。

新窗口先读取根/程序AGENTS、模块skill、原文件说明和当前计划，核对两个窗口交接及实际RK状态；再按授权选择一个业务小项继续。检测算法仍按用户要求暂缓，原参数和曲线保留，不伪造检测结果。此交接不自动授权旧窗口恢复工作。
