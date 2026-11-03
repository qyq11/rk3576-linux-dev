# 同步与任务错误第一片：冻结候选独立复核

2026-11-02 20:10（北京时间）。只读审查`sync_task_recovery/candidate/`，没有修改候选/正式源码，没有编译、运行、连接板卡或操作界面。

已核`source_manifest.json`的SHA-256为`b8cf492d423d3c937f70cf64943f1686ba2cbef4a0b046b300a5bfd2d2fd8be1`，14份候选逐文件指纹全部匹配（candidate_mismatches=0）。以下行号均指冻结candidate内部文件，不能套用随后总窗口合入后的行号。

## 需要修正的确定问题

### 1. 旧任务同内容Tr重复返回，仍可能把同位置的新预设标为完成

位置：`samples/original_detection_session.cpp:451`至`:465`、`:517`至`:525`；`samples/original_sample_repository.cpp:194`至`:210`。

可由源码确定的顺序：

1. 旧任务收到Tr，原曲线存档、数据库更新成功，任务加入finishedTasks_，其架解除inFlight保护。
2. 操作者对同架名、同孔、同项目、同重复索引准备一份新的本地任务，或已进入下一轮任务。saveRack允许在旧任务全部结束后保存这些内容。
3. 设备重发旧任务同内容Tr。issuedTasks_仍保留旧Task；冻结候选只拒绝曾被TR处理的taskErrors_，没有对已经Tr完成的任务作幂等终止。
4. 原曲线仍为“待计算”，所以同内容文件的`result/original`为空，`:465`不会提前返回。末尾`:522`再次调用updateDeviceTask；该函数只用架名、孔、项目和重复索引定位，没有设备任务号/预设版本，于是改到刚准备的新预设，把它置Finished。

若新架尚未发给设备，这会把新任务从可发送列表中移除；若正在新一轮，则会把新任务显示成旧任务的完成状态。重复结果不应再次改变已经复用位置的预设。需要对本进程已完成的结果记录其已确认报文摘要/存档身份，重复相同内容只确认已有保存，冲突内容明确拒绝；不能简单按任务号吞掉全部不同结果，也不能再调用当前新预设的updateDeviceTask。

这一漏洞在旧正式Tr路径已存在，冻结候选增加TR保护后仍保留。它是可确定的现有重复结果身份问题，不是要求改变原检测业务或放宽TR保守分支。

### 2. TR原因0实际绑定SQL NULL，未保留原版六项空文本

位置：`samples/original_detection_session.cpp:634`至`:640`。

原因表首项为`QString()`，这是空指针态的QString；经QVariant参数绑定，Qt5的SQLite驱动按空值处理，会写SQL NULL，其他七种原因则写TEXT。原`tool/database/report/report_result_table.cpp:325`把原因0转为空std::string，`:479`至`:485`通过sqlite3_bind_text绑定长度0的文本，结果为TEXT空串。SQL NULL与空TEXT并不等价，影响类型、查询和原程序再读取时是否存在该结果键。

将原因0首项显式写成`QStringLiteral("")`即可保留原空TEXT语义；父报告“测试失败”和任务Reset继续按已确认的原行为，不借此改变规则。本项为源码绑定语义核查，未运行SQL验证。

### 3. 新试剂位一致性检查会在设备缺字段时跳过

位置：`shell/device_control_shell.cpp:148`至`:159`的detectionStartBlocker。

目前只有generalFields.size()>20才检查第20项试剂位。当前parseStatus允许4至25个主字段，DeviceControl::startBlocker只强制至少14个；主字段共20项时，仍能含第19项LinuxNew版本分支，却不含第20项试剂位。若其余实际状态与同步已通过，配置试剂位已知而设备未报该字段，这个新检查直接跳过，与交接所述“无效或数量不符不能开始”不一致。

应在配置试剂位为正数时，将字段缺失与无效数值一并给出明确阻断；不猜42/48如何映射，也不改变两边原值。这是已新增保护的漏分支，不是要求增加新的仪器业务规则。

以上三项已分别发总协调处理。本审查不自行修改冻结包。

## 已具体核对的范围

### 无关与重复UP不会续时或撤ready

`OriginalDeviceSync::projectReply`先完整parse，再区分当前项目、已知非当前项目和已确认项目重复。已确认项目的重复还核本轮发送字段；未确认的其他项目不写库或借旧回包确认下一项。坏包、未知项目（试剂等待阶段之后）、同名字段不匹配仍失败。

`samples/original_sample_store.cpp`的UP分支只在success且!step.ignored时load和sendOriginalConfigurationStep；因此ignored不会进入启动10秒QTimer或发step.complete=false的逻辑。匹配的已确认末尾重复不取消同步ready。Ur之前UP只忽略，不缓存作未来确认，这是候选明确边界，未见误放行。

### TR整包预核和同事务写入成立

codec严格核原因0..7、完整数量、架/孔/任务号及包内重复任务；会话先核所有任务身份、已发记录、预设项目/重复索引、冲突/完成/连接不确定条件，再打开写事务。一个坏身份不会导致其他有效条目先退量。

报告六类错误文字、父报告state和试剂used_ul更新经同一SQLite连接附加试剂库后提交。试剂瓶按原表行id及孔名、试剂名称/类型/批次、瓶型、日期、区域核身份，数值有限且使用旧used_ul作条件更新；避免把预留量退到另一瓶。未见在commit之前移除reservedReagentRows_、写taskErrors_或finishedTasks_，因此SQL失败/提交失败时内存仍有原退量资格；Database关闭也会使未提交事务回滚。

提交后的updateDeviceTask在同一工作线程执行；本次预检与更新都按相同架/孔/项目/重复索引。仓库validateRack禁止孔内项目重复，inFlight阻止执行中的架被保存/删除，函数间没有事件循环或并行改预设入口。因此没有把“数据库已提交、随后必定更新失败”当作确定bug。旧Tr跨预设复用问题另见第1项。

### TD阶段8和TR重复保护

原Windows只有TD到ChannelState_Testing=8时才将rgHole全部isAdding=true；候选在核架/孔/项目、七字段类型及阶段后，仅state==8加入addedReagentTasks_。三字段通知不推断阶段，低阶段迟到不倒退；TR后的迟到8明确提示液量待核，不把Error复活。

相同任务同原因TR跳过再次报告/退量；不同原因TR、Tr/TD9后TR、TR后Tr、断线后无阶段8证据的任务自动退量均按候选已说明的保守待核边界拒绝。这些与原版的明确差异已有交接，不要求本轮改成新业务或放宽。原因0的SQL类型差异不属于该保守边界，已单列第2项。

### TF/TB和退出归属

TF仅回原版TF或TF[Emergency]；候选DeviceControl只额外准入这两个确切值，没有开放任意T命令或TK。TB整包核数量、架名重复和秒数，只更新已有架；单调时间戳和父子QTimer在原位置显示，最低1秒，不自动判完成。未见为显示TB每秒读取数据库或形成无主定时器。

TK仍明确未接授权码和保密日志，不能因此登记解锁功能完成。所有这些结论仅为源码范围；Windows实际运行、ARM64整合编译、板B实际收发/数据库/界面、真实仪器与资源测量均未由本审查新增通过项。
