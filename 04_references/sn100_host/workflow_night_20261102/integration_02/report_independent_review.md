# 报告明细切换候选独立审查（2026-11-02）

## 范围与结论

只读比较候选 `report_lis/candidate/history/history_page.cpp/.h` 与正式同名文件，并跟踪原Windows报告明细的可达调用。没有修改候选或正式源码，没有编译、运行、操作Windows/RK界面或创建测试数据。

候选CPP SHA-256：`9e803f5519ba31a18f752b9942c8005edcd942cf9a8cd686819f17cb10dedce7`。
候选H SHA-256：`44469eec6bff91e4333d20a376a45c067a69bfe5006577d26f328b52671d13bb`。

在报告集合稳定、后端按现有契约返回的条件下，**未发现本次改动导致的确定性越界、跳错固定行、过期回复覆盖新明细或未经确认丢草稿问题**。本结论是源码审查，不是跨记录、跨页或原Windows实际运行通过。正式发布仍需由总窗口完成ARM64整合及板B原资料实际核对；空表检查不能覆盖本切片。

## 原Windows实际可达路径

- 原 `frm_report/frm_report_material.cpp:2003` 新建明细，:2010–2011把明细的SignalPrvRowButton/SignalNextRowButton接到主表on_btnPrvRow_clicked/on_btnNextRow_clicked，初始索引为当前块内页×每页条数+当前行（:2013）。
- 原 `mask_dlg_report_detail.cpp:581` 上一条先在缓存块里减index，块首时reload前块并用最后一条，然后发主表上一行信号、RefreshReportDetail。
- 原同文件:596下一条先加index，块尾时reload下一块并取首条，再发主表下一行信号、刷新明细。
- 主表原上一行:1792在页首回前页最后一行；下一行:1821在页尾进后页首行，边界返回。明细和主表必须一起移动，不能只改变弹窗内容。
- 原实现以缓存块加页面组织；当前程序后端每次查询20条。候选延用当前20条查询入口，不直接移植原大缓存块，这属于既有数据访问方式适配；相邻顺序、筛选条件和主表选择应保持。

## 候选已覆盖的检查

| 核查点 | 实际调用及判断 |
| --- | --- |
| 同页上一/下一 | moveDetailRow:1323按m_detailReport.id反查当前页，而非只信主表当前行；同页取row±1的报告id |
| 跨页 | :1348仅改offset±20，保留日期、样本号、条码；continueDetailNavigation:1367对offset和非空页做检查，上一页取last、下一页取first |
| 首末边界 | setBusy:958–964计算offset+明细所在行；moveDetailRow:1330–1331再次检查0≤目标位置<total，按钮禁用之外还有入口核对 |
| 主表同步 | acceptDetailNavigation:1383仅在目标明细成功返回后安装候选页、total及筛选；:1390按返回report.id选择主表同一行，然后showDetail |
| 旧回复 | query和detail回调都核m_pending与m_operation；requestDetailReport使用单调递增serial并记录期望reportId，普通路径:507及local路径:375再次核report.id；旧序号不能覆写 |
| 查询后明细失败 | 页码、reports、total及主表选择尚未安装，finish清除候选导航；不会把目标页先显示成已成功 |
| 草稿保留 | moveDetailRow先调用confirmLeave；默认/ESC为Cancel，取消不发查询/明细请求，旧草稿继续保留；明确Discard才fill旧成功快照后继续，不自动保存患者 |
| 在途操作 | 页面已有isBusy包含数据、报告输出及字典；新增入口与按钮均按同一门闸阻止，患者编辑区和关闭入口在忙时禁用，close守卫也拒绝 |
| local格式过期 | 新请求经过HistoryCurrentDisplayState的expect与本次来源快照校验；配置/身份变更使pending失效，迟到值不能恢复旧格式 |
| finish参数引用 | 复制finishedOperation再清m_operation，修正传入m_operation引用后名称被清空的风险；普通query/detail现在也会清pending，减少重复回调被再处理 |

## 必须说明的限制与后续核对

### 1. local结果模式失败时不能宣称旧“完整明细”原样保留

候选 `requestDetailReport:1312` / 跨页:1355调用expectDetail/expectQuery。`HistoryCurrentDisplayState::expect`会立即clear并发cleared；页面`clearCurrentResultPresentation:593`把旧主表、旧明细的results清空，保留身份和患者草稿。目标查询或明细失败时，旧报告的患者/身份仍在，但旧结果列表已经清掉。

这是既有单一当前格式状态的保守失效规则，避免显示参数可能过期的结果；不能为了视觉回滚而简单恢复旧数值。新增切换首次把该规则带进“弹窗保持打开”的路径，文档“切换成功前保留原明细”应改成“保留原报告身份、患者信息和主表选择；结果值按当前格式失效规则处理”。若产品要求失败后继续显示旧结果，须单独设计带来源校验的旧报告重新读取，不能把旧缓存当仍有效。本轮不把这项列为确定的串记录错误。

### 2. 数据集在后台变化时，offset不保证仍是严格相邻

候选continueDetailNavigation只核offset和非空；没有把当前明细的id作为跨页查询锚点，也没有报告集合版本。后端 `HistoryStore::query:818`按id排序、LIMIT/OFFSET，查询和接下来的detail为不同事务。

例如用户停在原第21条，后台隐藏其前一条后，上一页20条的末尾可能已包含当前记录；再点上一条会回到同一记录。下一页也可能跳过一条。原Windows使用offset重载块也有同类动态集合边界，当前主列表原有跨页同样如此，所以这不是本候选新引入且已证实的静态翻页错误。实际联机若同时产生/隐藏报告，应检查此行为；若要承诺严格相邻，需后端按当前id及筛选找邻居或先重新定位当前记录，而非单纯检测total改变（隐藏与新增可抵消total）。

### 3. 明细成功数据与主列表摘要的时效分开

acceptDetailNavigation安装的是先前查询或同页旧快照中的摘要；没有用新detail返回替换该行摘要。后台改过患者资料时，明细可显示最新姓名，主表该行仍是查询时姓名。主表选中id正确，但不能描述为所有字段同步刷新。原Windows关闭明细会SlotReloadReportData；local模式当前关闭也会refresh，普通模式沿原有实现未新增关闭刷新。若本轮仅验前后切换，记录这项已有缓存边界即可；若承诺主表资料立即一致，应更新相同id的摘要或安排安全刷新。

## 建议的实际核对范围

在现有原资料有至少21条匹配报告时核第1条、同页中间、20→21、21→20及最后一条；每次核弹窗报告id/条码与主表选中id、页码、查询条件。编辑患者字段后分别选“保留更改”和“放弃并继续”；保存正在进行时检查按钮和关闭守卫。现场出现查询/明细失败或配置失效时确认患者身份不串、旧结果不冒充有效；没有可用报告或不能触发分支则明确待验，不另造匿名库、模拟服务或回环用例。
