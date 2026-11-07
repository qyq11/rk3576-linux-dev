# LIS B1只读映射快照与孔自动编号标志

2026-11-04，供唯一总控01a1a62e-3f89-7e62-98fa-d44cffa11f5c轮询。候选已就绪；本窗口不改正式/公共源、不编译或联板、不提交推送，也未向其他窗口再派任务。

基础为当前正式021d4734915e4dac863155876851f974e365c92f，697源实体核对等07保全后的22f15e91清单。旧temp已不存在；从07保全包按原路径/长度/SHA只恢复15份本片依据。on15三目标后像与正式三源一致，未重复合入旧候选。原件位于inputs，实际结果在restore_result.json。

修改只有candidate/06_programs/sn100_host/samples下三份文件：

- sample_types.h：补原孔isAutoId默认true，独立于患者patient.isAutoId默认false。LIS两次清患者及回复应用仍拷贝/保留原孔标志；未把它接入报告或改手工编号规则。
- original_sample_repository.h：新增不可直接改成员的OriginalLisMappingSnapshot，以及captureOriginalLisMappings只读接口；仅有值，不是应用或设备许可。
- original_sample_repository.cpp：原映射读取/排序/摘要规则不变，封装完整输出后供现B1内部A9计划使用；读取失败输出无效空快照。孔私有输入摘要升级v2并包含新标志；阶段、校验、内存发布和失败恢复不变。

原版对应：frmSystemMaterial::loadAllProject/loadProject→SqlBase::getData→LisCommReceiver::slotReceiveLisCommBytes项目映射；S_SampleHoleInfo构造默认；frmSampleMaterial::slotPCGetLisTasks及LisCommSetting::QRYMessage两次清患者；原DSR拷贝/写回孔资料。逐项链表、行号和边界见source_review.md，所读源码指纹见read_basis.json。**Windows核对仅只读源码，没有Windows实际运行或重建。**

| 交付 | 位置/结果 |
| --- | --- |
| 普通三源窄补丁 | packaging_01/lis_B1_snapshot_auto_id.patch；SHA-256 43861a78914e997492a08b3a17d8cfa903c4e22914baafa558bcd845643d89bf |
| 精确前后SHA | before_target_manifest.json、after_target_manifest.json及target_changes.json |
| 源码/补丁核对 | packaging_01/result.json；8步均实际0，普通check/apply 0/0，三份回放0差异、CRLF保持，正式697源复读0漂移 |

交接接口：同一Repo线程调用captureOriginalLisMappings；成功再读valid/projects/projectSourceDigest/digest。该值不延长来源有效期，不接受它替代真实消息或旧target。现prepareOriginalLisApplication仍在Repo内部读取并核目标，apply仍只接Repo自己完整生成的私有Prepared。v2只标识仓库本地摘要，不是设备报文或配置键。公共信号、元类型与生产消费者均未新增。

尚未验证：未编译、未运行SQL/产品/测试/模拟、未发包、未连板或占显示。源顺序跨平台、板A原资料、真实LIS/仪器、响应和资源待总控。共享任务摘要、孔标志随Task进入原reserve及报告、B2/B3完整收发、报告分组和急诊来源仍待；不把补内存字段说成患者已持久保存。采用新值对象后需完整重编所有消费者。

本片没有新增门槛或放宽校验。原B1严格失败/保护、A9预算及无ORDER BY/非稳定同号排序的已知差异保留待核。总控先核三份前像再选择采用；未通过编译/RK前不标功能完成。
