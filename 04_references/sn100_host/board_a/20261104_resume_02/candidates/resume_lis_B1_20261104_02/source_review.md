# 本片源码核对

2026-11-04。当前提交为021d4734915e4dac863155876851f974e365c92f。正式保全包中列出的697份源码已实体复读，全部等于formal_source_after.json；原on15候选三份后像也与当前正式三源逐字节相同。本片从这些正式字节接续，没有重放旧累积补丁。

旧temp已不存在。restore_inputs.py只从07_backups/sn100_host/source_candidates/20261104_before_temp_cleanup/development_files.tar.gz读取15份相关源、交接和依据，按development_files.json逐项核长度及SHA-256，未改归档。原field_crosscheck.md、B1第三轮和on15历史说明保留原字节；其中的“未采用/暂停”是旧时点，由当前正式记录和本次恢复分派承接。

Windows依据全部是只读源码。本轮没有运行Windows原版或重新构建，也没有执行SQL、产品、测试、模拟器或网络操作。以下行号是read_basis.json所绑源码的行号。

| 用途和原入口 | 原版实际行为 | 本片对应及失败边界 |
| --- | --- | --- |
| 读取项目映射：frmsystemmaterial.cpp::loadAllProject，689—741 | project目录按*.db及Files/NoSymLinks取文件；名字含小写formula则跳过。先按xuhao.toInt用非稳定std::sort，再按config.ini的sendConfig/sendSequence首个匹配前缀稳定排序，前缀不分大小写。 | captureOriginalLisMappings沿用原B1读取体。没有新增同号次序、过滤、截断或去重。 |
| 读取映射字段：同文件::loadProject，2092—2138；sqlbase.cpp::getData，762—807 | 基础name/value同名键后行覆盖；结果代码来自project_results.lis_code；默认稀释按project_sample_rate实际行序取首个sample_default_option.toInt非零项。getData未指定ORDER BY。 | 仍用literal select *与query.next读取映射所需的三张表，不用编辑缓存、Database::rows的ORDER BY或带recoverPending的项目load。SQL打开参数false及既有版本检查均未改。 |
| 原映射使用：liscommreceiver.cpp，280—359 | 名称首匹配；无名称匹配时结果代码后项目可覆盖；随后按基础名称另首查资料。查不到资料仍可建id0/空稀释任务；全未知时仍整体替换为空任务。 | A9纯计划代码未改。prepareOriginalLisApplication仍只用同Repo自己读取的完整映射，失败不拿默认值应用；不接受外部快照或计划作为应用许可。 |
| 孔位标志：cussamplehole.h::S_SampleHoleInfo，475—495、536 | 孔isAutoId默认true，独立于ReportItemData.is_auto_id默认false（report_table.h:32）。 | 新增SampleHole.isAutoId=true；patient.isAutoId仍false。私有孔输入摘要升级v2并包含孔标志。没有依据把标志改成“收到LIS号即false”或从患者标志推断。 |
| 扫码及查询：frmsamplematerial.cpp::slotPCGetLisTasks，4098—4102；liscommsetting.cpp::QRYMessage，732—756 | 扫码先清整份患者；生成有效查询再清一次患者。两次都不改孔isAutoId、内部号和旧任务。无效条码第二阶段只置MAX。 | 既有两阶段函数体不变：从实际旧孔拷贝，仅重置patient，因而保孔标志。没有加自动编号、改查询起时或清旧任务。 |
| 回复应用：liscommreceiver.cpp，277—403 | 用getSampleHoleInfo拷贝原孔资料，映射循环只改原有指定字段，再整份设回孔；没有改isAutoId。 | prepare仍以SampleHole after=before开始，再赋原计划字段；apply/publish的整份交换不变，所以新孔标志随原孔保留。 |
| 后续报告占号：frmsamplematerial.cpp，2086、2750 | 持久化前用hole_info.isAutoId覆盖报告is_auto_id。 | 本片仅补内存来源，没有改Task、reserve或报告SQL；当前硬编码报告标志等缺口仍交后片，不声称已经落库一致。 |

新增OriginalLisMappingSnapshot仅能读取valid、项目来源摘要、映射摘要及有序项目值。成员私有，默认无效；调用读取时先清掉输出，只有全部原有读取/版本检查成功后才交出完整有效值。有效空项目集仍有效，不混同读取失败。快照副本可以保留供只读核对，但不证明当前资料仍有效，更不证明消息、患者保存或设备执行。没有外部快照应用入口。

本片映射读取体从beforeSource到最后afterSource核验的字节保持，映射摘要的序列化字段及顺序也保持。仅更换输出包装和B1调用点；既有线程、INI、路径、只读连接、文件变化、SQL错误以及A9拒绝仍返回失败。患者与任务的实际内存发布和阶段函数体未改，失败不提前MAX，不回滚已发生的R1，也不生成ACK。

package_candidate.py在私有三源副本生成普通文本补丁。diff空白检查、普通apply --check与apply均实际0，三份回放后像与候选SHA一致，保CRLF。九处编辑按逆序还原后逐字节等三份base，限制变化范围；这只是源码字节核对，不是编译或功能测试。packaging_01/result.json保留每条实际命令和退出码。打包时再次实体复读正式697份，0差异；当前公共文档修改属于总控，未触碰。

待核与交接：

- 原无ORDER BY的表行及非稳定同号排序在两平台/实际资料上的结果，仍须板A原资料核对；不补新排序来掩盖未知。
- 原Windows对坏表/部分读取可继续，当前B1已有严格失败、工作根许可标记和整架在途保护；本片保持这些条件，不能把既有差异称已与原版全等。A9现有数量/文字预算和年龄溢出拒绝也仍属既有待核。
- 孔isAutoId已进入B1私有摘要；共享User/taskInputDigest、设备Task携带和报告持久化仍未接该值。新字段影响值对象布局，总控采用后须完整重新编译所有消费者，不能混旧对象。
- B2/B3、真实消息来源/首匹配/等待寿命、一次继续、报告分组、QRY/ACK及急诊可信起点未做；不得启用半条生产下载链。
- 没有新增映射扫描次数、状态展开或业务数量门槛。实际SQL耗时、源文件反复指纹成本、内存、响应及同板影响未测，均交总控按板A条件安排。

本片交付范围只到候选源码和私有补丁重放；完整编译、板A运行、真实LIS、仪器和资源验证全部待总控安排。
