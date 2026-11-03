# 保存生产桥与正式第六批底稿对照

结论：未发现底稿漂移。producer v2清单的17个补丁目标中，16个有既有底稿，均与正式06官方源码包逐字节一致；另1个是新增许可头，在正式06中确实不存在。捕获清单额外保存但不进入补丁的`shell/frmmain.h`也逐字节一致。不存在需要补桥的业务内容差异，也不存在这两份底稿之间“只有CRLF不同”的文件。

这只证明底稿适用，不代表补丁已整合、普通字节apply已通过或功能已运行。本次没有apply补丁、构建、执行产品或访问库/网络/板卡；没有修改正式源、06review、N或固定producer候选。只新建本报告、指纹JSON及从官方源码包展开的自有基线副本。

## 使用的正式来源

官方归档：`07_backups/sn100_host/board_b/workflow_integration_20261102/integration_06/`，release id为`14ce9714aaa0017c7ad78ff3f551d978672e5def79b96e817605295d7e59020c`。

| 对象 | 实际SHA-256 |
| --- | --- |
| `sn100_sources_14ce9714aaa0017c7ad78ff3f551d978672e5def79b96e817605295d7e59020c.tar.gz` | `c1a21586e8b9a1adaaefd9ac1743f8b0f74c04a45f37001450de61b42402369b` |
| 官方`source_snapshot.json` | `48c4010b1e4b74fc3ff0664cb16d6981c2e6c7fe117aaaf54cf39790ae3bb213` |
| producer v2 `candidate_manifest.json` | `9de4df264ffb34a57ddccd252b2b2710bdd0894c0362ce6805428da57c4d0b9f` |
| 工作区根`.gitattributes`，以及基线根的逐字节镜像 | `5de2ac8a8bf53f9c7775f129eac002896013a7d2415ae731a679616852a600a1` |
| 本次逐目标指纹`producer_base_fingerprints.json` | `a7bded190111a425a18f1374515710a934a51896c7863642e7277ced92e22495` |

先核官方包及snapshot与归档SHA256SUMS/来源记录一致，再核包内649项均为普通文件、成员名集合精确对应snapshot，无绝对路径、上级目录或链接。只展开到本任务自有`integration_07_review/producer_baseline_06/`；没有覆盖其他目录。展开后649/649文件SHA符合官方snapshot，同时当前正式`06_programs/sn100_host/`的649/649也符合该snapshot。

基线根另放当前根`.gitattributes`的原字节副本；它不计入官方649份SN100源文件。该目录只是可核对的官方基线，没有创建晋级脚本或应用任何候选，也未初始化为可推送的工程仓库。

## 17个目标的实际匹配

| 目标 | 结果 |
| --- | --- |
| `projects/original_project_repository.cpp/.h` | 两份captured_base均与官方06原字节一致 |
| `projects/project_page.h` | 原字节一致 |
| `projects/projects_store.cpp/.h` | 两份均原字节一致 |
| `reagents/original_reagent_editor.cpp/.h` | 两份均原字节一致 |
| `reagents/original_reagent_store.cpp` | 原字节一致 |
| `reagents/reagent_store.cpp/.h` | 两份均原字节一致 |
| `samples/original_device_sync.cpp/.h` | 两份均原字节一致 |
| `samples/original_sample_store.cpp` | 原字节一致 |
| `samples/sample_store.cpp/.h` | 两份均原字节一致 |
| `shell/device_control_shell.cpp` | 原字节一致 |
| `projects/original_project_save_admission.h` | captured_base=null，官方06无此文件；是本片新增，不是漏核 |

额外捕获但未进producer补丁的`shell/frmmain.h`也相符。逐文件原SHA、字节数和行尾计数均在指纹JSON中，全部捕获底稿也核过符合producer自己的captured_base。

这里必须区分两件事：**captured_base对官方06**是16份逐字节相同；**producer candidate对其captured_base**则有业务改动，且候选17个目标全部为LF、官方既有16个目标为CRLF。后者是补丁发布时的行尾边界，不能拿“归一化相同”掩盖真正业务改动，也不能因本次底稿一致就把旧的`--ignore-space-change`检查说成普通字节检查通过。

本次另用严格UTF-8解码，只替换CRLF→LF、不忽略其他空白，计算两份底稿归一化SHA：结果仍全部相同，统计为16 byte_equal、0 crlf_only、0 content_difference、1 new_file_no_captured_base。因此没有因新正式06内容不同而需要另写兼容业务桥；后续仍应在匹配`.gitattributes`的隔离根按窄片整合和核最终字节。

## 第六批必须保留的既有修点

下列代码都已包含在本次匹配的06基线里，不是缺失修复。后续只加producer时须保留，不能从旧250b、旧copy或compile_support整文件回拷替代。

- **CAL真实提交通知及写守卫。** 官方`samples/original_sample_repository.cpp::saveCalibrationParameters:274-328`冻结target和expectedFileName，项目锁后先`projectRepository_.writesAllowed`，实际COMMIT才记commitSeen；写连接关闭后仍持锁核同一文件Record/SHA，再锁内通知。重读失败仍通知已提交、verified=false。此文件及头不在producer补丁内。producer所改`original_sample_store.cpp`中的既有CAL生产回调同样须保留，不由普通reload产生新notice。
- **UP已提交不能因后续同步失败而消失。** 官方`OriginalDeviceSync::projectReply:125-146`保留新DeviceRepo结果签名、同一filename定位和`written.commitSeen`，`nextProject`重置step后再次恢复commitSeen。`SampleStore::receiveDevicePacket:277-299`遇step.commitSeen即尝试重读/发布实际项目变化，失败仍报已写入，不让旧项目缓存继续冒充当前快照。这是68b5修点，不能退回仅success时刷新。
- **发送结论晚回只核清其真实来由。** 官方`OriginalDeviceSync::projectUpdateSendResult:334-359`仍匹配intent/连接，区分prepared与仅等入队结论的unknown，并保留`activeSendConclusionKnown_`、`activeSendMayHaveQueued_`及not-attempted-proof。只有确未尝试的结论能解释相应超时；可能已发送仍保未知，不自动重发或清其他任务事实。新预留预算不能代替这些已有发送事实。
- **初始/单项UP写入的copy守卫及稳定文件身份。** 官方`samples/original_project_device_repository.cpp::applyDeviceProject:158-218`仍用项目锁后noPending、新expectedFileName/结果签名、真实commitSeen及同文件Record/SHA核对；不是旧的按可变id寻找目标。该CPP/头不在producer补丁内，不能回退。
- **项目读者和所有写路的copy边界。** `OriginalProjectRepository::readProjects/load`仍在完整局部读取成功后发布，过滤仅已证自有未提交clone，unknown拒绝；写路用纯noPending/writesAllowed，不把只读过滤授权成可写。`OriginalReagentEditor::mutate:383-387`仍按项目→试剂两锁，只有`project/`写入在两锁后noPending。producer增加锁内提交通知不能删这些条件，原copy证明helper及外部写守卫也不在本片范围。
- **Root统一通知、原权限和未知事实。** 官方`device_control_shell.cpp`的acceptOriginalProjectSave、实际发送前期限/身份/intent核对及原prepared/unknown消费继续保留；producer只加两个真实来源预检/提交接线。不能把预留允许当成Up已确认，不能删canStart的全项目版本保护或清Us、优先试剂、任务未知。正式主入口、Uh、Qq和构建文件不由本producer变更。

指纹JSON另列CAL仓库、DeviceRepo和copy helper这6份补丁外上下文的官方SHA；与本自有基线相符，明确这些文件不是可用旧副本覆盖的输入。

## 本步交接边界

producer v2底稿相容，无新增业务兼容阻断；其总10秒修复结论仍见`project_save_producers_v2/fix_review.md`。本步没有把producer应用到正式或本基线，没有引入N，没有构建、运行或生成正式晋级工具。下一步由总协调选定第七批范围后再按固定候选顺序隔离整合并核最终清单；N03另行窄复核，不借本次底稿一致推定N可合入。
