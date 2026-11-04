# 结果、报告、校准、质控与 LIS 操作流程只读审查

本报告核查 Windows 原版的实际操作顺序与当前 Linux/RK 正式工程，重点是“新增或编辑后何时进入列表、何时写数据库、保存或取消会改变什么、切换记录或关窗前是否确认”。本次只新增这份分析报告，没有修改业务代码、共享计划和记录，没有构建、运行、联板、提交或推送 Git。

读取日期为 **2026-11-04**，关键工作区文件指纹读取时点为 **12:05:01 +08:00**。本次依据当前工作区，保留已有未提交修改；其他窗口仍在工作，本文不是整树冻结快照。后续采用结论时应先核文件指纹和行号。

**主要结论：**正式页面和若干数据库处理已经接入，但保存、取消、复制和切换的行为仍有确定差异。最突出的是：校准“编辑后复制”变成只能复制已保存记录；质控原来立即写入的一些动作被改成等外层保存；患者明细关闭、切记录和 LIS 离页增加了原来没有的确认。不能只因“页面存在、能够保存”就判断原操作已经完整移植。

这里不是认定原版完全没有未保存输入。原版也有临时的界面输入；差异在于移植版把它扩大成跨记录、跨规则的统一草稿，并改变了原来实际写入和放弃的时点。对原版不理想的处理只如实记录，本报告不决定修复或改业务。

## 1. 范围、状态和接入依据

| 标记 | 含义 |
| --- | --- |
| 已核 | 已追到本表所述入口、处理和数据变化；不代表已在 RK 或真实仪器验证 |
| 确定差异 | 两端可达代码在相同操作下存在不同的写入时点、可执行条件、取消结果或可见结果 |
| 未接 | 源码或部分基础处理存在，但主程序没有相应生产调用，或正式入口仍禁用 |
| 待核 | 尚缺原分支、有效输入、运行结果或关闭机制的依据，不能作确定结论 |

正式校准和质控页分别由 [main.cpp:1094][n-main-cal]、[main.cpp:1017][n-main-qc] 创建并交给 [attachCalibrationPage:11][n-cal-shell]、[attachQualityControlPage:52][n-qc-shell]。历史页的查询、明细、患者保存、显示设置、CSV 和报告控制器均在当前统一工程中；本文逐项区分原数据库模式和额外的本地结果模式。当前 LIS TCP 控制器已由 [main.cpp:1451][n-main-lis] 接正式主程序，不再沿用“仅配置、未打开 TCP”的旧结论。

本轮静态核到：校准编辑/保存/复制/删除/审核/应用/方法弹窗；质控品新增、管理、项目选择、规则、批次；历史查询、患者明细、报告行切换、显示设置、导出选择、文件路径、预览打印入口；LIS 设置和完整消息交接；算法及结果写入的断点。跨午夜自动刷新、各 Mask 的全部 Esc/系统关闭机制、全部快速连续操作结果及实体打印未完成核实，见第 8 节。

## 2. 校准操作覆盖矩阵

校准用于用已知浓度的材料建立换算关系，之后把检测信号换成结果。原版“校准任务生成记录”和“在校准页复制已有曲线”是两种创建来源，不能和移植版额外的空白本地记录混在一起。

| 子流程 | 覆盖结论 | Windows 实际操作及写入时点 | 当前正式工程及使用者差异 |
| --- | --- | --- | --- |
| 打开项目、选择曲线 | 已核；离开条件存在确定差异 | [slotProjectSelectChanged:329][o-cal-select] 直接重新读库、刷新项目曲线；曲线点击 [init_components:193][o-cal-click] 直接刷新当前记录，没有统一未保存确认 | [CalibrationPage 构造:120/135][n-cal-select] 先调用 prepareToLeave；[prepareToLeave:751][n-cal-leave] busy 拒绝离开、dirty 弹“放弃/保留”。原来直接切换的动作现在可能被阻止 |
| 当前点值编辑→保存 | 已核；无修改及失败处理存在差异 | [on_btnSave_clicked:1299][o-cal-save] 总是询问；[update_curve_table:1246][o-cal-update] 先写基表、再读当前表格并写原始点、最后重新读库，未统一检查每笔写入结果 | [saveCurrent:619][n-cal-save] 无 dirty 直接返回；读编辑值后排队保存，busy 禁止重入；[saveOriginal:193][n-cal-store-save] 作校验和事务写入。原来分步写入的失败结果与新事务失败结果不同 |
| **当前点值编辑→复制新增** | **确定差异** | [on_btnCopy_clicked:1311][o-cal-copy] 确认后调用 update_curve_table(true)；[update_curve_table:1260–1293][o-cal-update] 先生成新编号和基记录，再由当前界面 get_calibration_result_data(true) 取得点数据。允许“先改值、直接复制成新曲线” | [copyCurrent:634][n-cal-copy] dirty 时直接返回；只传已保存 id/revision 给后台复制。复制按钮也由 [updateActions:284][n-cal-actions] 禁用。原来的操作序列不能完成 |
| 删除当前曲线 | 已核；确定差异 | [on_btnDelLine_clicked:1328][o-cal-delete] 先检查是否被批组使用，再询问并隐藏；没有 dirty 前置 | [deleteCurrent:643][n-cal-delete] dirty 时拒绝，还要求项目存在且启用。该限制不只是异步等待 |
| 审核 | 已核基础业务；未保存编辑条件有差异 | [on_btnCheck_clicked:1385][o-cal-review] 检查 power1，确认后写审核标志并重读 | [reviewOriginalCurve:663][n-cal-review] 已调用后台审核；但必须 !dirty，按钮也按此禁用。不能整项写“审核未实现” |
| 选批组应用、Default 应用 | 已核基础业务；未保存编辑条件有差异 | [on_btnApply_clicked:996][o-cal-apply]、[slot_default_batch_set_curve:1052][o-cal-default] 检查工程师身份；需要时另行审核并绑定批组 | [applyOriginalCurve:674][n-cal-apply] 已接对应批组和 Default 绑定，保留“先审核后应用、应用失败不撤审核”；额外要求 !dirty、启用项目、完整目标绑定快照 |
| **autoApply 与未审核应用** | **纠正旧误判：原规则已保留** | [get_project_is_checked:476][o-cal-checked] 在项目 auto_apply 为真时即返回 true，否则看 checked。因此原版并非所有人工应用都先弹审核 | [applyOriginalCurve:686–703][n-cal-apply] 同样只在“未审核且 !autoApply”时要求审核后应用；[canEdit:94 / canApplyOriginalCurve:101][n-cal-rights] 分别查 power1 和 role≥4。不能把 autoApply 绕过提示写成移植版新增错误 |
| Cancel | **确定差异** | [构造:41–43][o-cal-cancel] Cancel 调 slotProjectSelectChanged，后者从数据库重新读取，不写当前输入 | [cancelDraft:656][n-cal-cancel] 从 m_records 缓存 showRecord(id)，不重新读数据库。外部改变数据库时，两端 Cancel 后显示的数据来源不同 |
| 当前页关闭、跨业务离开 | 确定差异；全局提示顺序由总控另审 | 原项目/曲线切换没有统一 dirty 守卫；原界面切页不等于自动保存 | [prepareToLeave:751][n-cal-leave]、[closeEvent:770][n-cal-close] 新增草稿确认。此处只证明页面守卫，不能把所有全局切页、注销、退出的确认顺序都视为已核 |
| 额外“新增本地原始点” | 已核为新增入口；不是原页新增功能已还原 | 原页本轮确认的新增入口是复制；另有样本校准任务创建曲线。未找到原校准页可达的“新建空白本地记录”动作 | [startNewRecord:466][n-cal-new] 新增对话框，确认后先建界面草稿、再由 Save 落库，点数 1–32、重复 1–8。应单独说明这是额外业务模型 |
| 方法、X/Y 变换弹窗 Save | 部分已接；后续计算未接 | [MaskDlgCalculateIdSetting::on_btnSave_clicked:65][o-cal-method-save] 用原编号读记录，改 method/x/y，Update 成功才关窗；失败留窗。父 [on_btnCalculateIDSetting_clicked:1442][o-cal-method-parent] 弹窗返回后重读并立即 on_btnReCalculate_clicked | [editOriginalMethod:733][n-cal-method] Save 先 accept 关窗，再排队 setOriginalMethod；写失败由父页反馈，弹窗已关。没有接原“重读后重算”步骤，不能只认定字段保存即整个方法设置完成 |
| 方法弹窗 Cancel/top X | **确定差异** | 原弹窗 [on_btnClose_clicked:60 / on_btnCancel_clicked:76][o-cal-method-cancel] 只关窗、不保存；但父函数在弹窗返回后**无条件**重读并重算，即取消也会触发重算 | 新 [editOriginalMethod:747–749][n-cal-method] 非 Accepted 直接返回，不写方法，也不重算。原有“临时输入只在 Save 写库”的结构确实存在；不能笼统说所有临时输入都是新增 |
| 真实拟合、重算、原曲线放大 | **未接** | 原 [on_btnReCalculate_clicked:1181][o-cal-recalc] 和拟合/逆变换分支调用供应商算法；原曲线操作 [on_btnShowChart_clicked:1107][o-cal-chart] | 新 [updateActions:292–308][n-cal-actions] 仍禁重算、原图等；[showRawChart:760][n-cal-raw] 是额外本地散点，不等价原拟合曲线。算法依赖单列，不能掩盖以上非算法的操作差异 |

## 3. 质控品、规则和批次操作覆盖矩阵

质控是定期用已知范围的材料检查检测是否稳定。控制品名称、项目关联、批次、规则“选用”和规则参数在原版中并不是一笔统一保存。

| 子流程 | 覆盖结论 | Windows 实际行为 | 当前正式工程及使用者差异 |
| --- | --- | --- | --- |
| 新增质控品→进入列表 | 已核；基础写入时点相近 | [slot_btn_add_control_item_clicked:324][o-qc-add] 打开新增窗，返回非空名称/厂家后立即 Insert 并刷新列表，不等外层 Save；新增窗 Save 校验非空，top X 清输入 | [showAddControl:676][n-qc-add] 子窗 Save 即发保存请求，成功回调 [219–237][n-qc-success] 关闭新增子窗并重读。新增后进入列表的时点基本对应；新增/关闭前另加未保存确认 |
| 外层管理 Save | **确定差异** | [slot_btn_save_clicked:364][o-qc-save] 先写名称厂家，再只写当前选中的规则；失败可能已经写前一步。成功 :412 关闭管理窗 | [saveManager:664][n-qc-save] 收集全 draft；[saveOriginalControl:172–205][n-qc-store-save] 事务保存所有项目下所有规则。成功回调不关闭 manager。连续改几条再 Save 的写入集合、失败后数据和窗体状态均不同 |
| 外层管理 Cancel/top X | 确定差异；原并不撤销已立即写入动作 | 原 [构造:84–89][o-qc-close] 直接关窗；未点击 Save 的当前参数输入不写，但此前已添加的品/关联、已切批次、已勾“选用”仍已在库中 | 新 [discardDraft:464 / requestLeave:471][n-qc-leave] 和 [showManager:507–509][n-qc-manager-close] 新增统一放弃确认。后台已提交的独立动作仍保留，但规则“选用”已被纳入草稿，可随取消丢掉 |
| 勾选规则“选用” | **确定差异** | [set_quality_result_table:501→510→518][o-qc-used] 点击立即 SetRuleEnable 写库，不等外层 Save | 新 [showRuleDraft:641–645][n-qc-used] 只改草稿，须外层 Save。勾完直接取消，原保留、新丢弃 |
| 切质控品 | **确定差异** | 原 [slot_control_list_select_change:121][o-qc-select] 从库重装字段/项目，不维护一组跨品草稿，无放弃确认 | 新 [showManager:517–527][n-qc-select] 先询问是否放弃，取消则恢复选择，确认才换品 |
| 切项目/结果规则 | **确定差异** | 原按选择重装当前规则；当前参数只有外层 Save 才保存，不能把“未 Save 的当前字段”说成原完全没有临时状态 | 新 [showManager:529–544][n-qc-rule-select] 在切换前 rememberRuleDraft，保留多条规则尚未保存的值；最后统一 Save。新旧临时状态的范围不同 |
| 质控项目选择→Save | 已核立即提交；失败处理另有差异 | 原 [slot_add_control_project:159][o-qc-projects] 子窗返回后立即按选中集合增删关联及规则，不等管理 Save | 新 [showProjectChooser:735–742][n-qc-project-save] 明确 Save 才发 setProjects，后台事务处理；成功即关选择窗并重读。原逐笔失败与新事务整体失败尚需进一步逐项列写入结果 |
| 项目选择 top X/底部取消 | **确定差异，保留原特殊行为说明** | 原 [QualityControlItemAddProject 构造:16–27][o-qc-project-close] top X 清选择但不置 isClose，底部取消置 true；[isClose 初值 .h:30][o-qc-project-flag] 为 false。父 :178 只有 true 才跳过，否则 :181–192 删除未选关联。因此 top X 可走清空关联路径 | 新 [showProjectChooser:731–742][n-qc-project-save] top X、取消和 Esc 均 reject，仅 Save 写。不能把新关闭语义直接写成原版一致；原 Esc/系统关闭是否同样走父集合处理另列待核 |
| 删除控制品 | **确定差异** | 原 [slot_btn_delete_clicked:352][o-qc-delete] 直接按主键删除；[TableDal::Delete/ DeleteWithId:390–420][o-dal-delete] 无历史保护 | 新 [removeOriginalControl:207–216][n-qc-delete] 有历史禁止删，并删除规则、关联和批次。是否允许删除及删后剩余数据均不同 |
| 添加批次→列表/连续添加 | **确定差异** | 原 [MaskDlgControlBatchSetting 构造内 Add 回调:52][o-qc-batch-add] 按输入立即插入，每次都是 Add；成功不清输入，无选行转编辑的处理 | 新 [批窗:767–779][n-qc-batch-add] 可新增或更新 batchEditId；成功 [230–235][n-qc-success] 清输入。连续重复点按钮的语义不同 |
| 选择批次→编辑/新增 | **确定差异** | 原行选择只供删除等使用；没有当前新实现这套“保存修改/新增批号”状态转换 | 新 [批窗:787–818][n-qc-batch-edit] 切批号先询问放弃，再加载字段、按钮改为保存修改；Ctrl+N 另转新建并清字段 |
| 批次删除 | **确定差异** | 原 [MaskDlgControlBatchSetting 构造内删除回调:95→102][o-qc-batch-delete] 直接 DeleteWithId，无当前/前批/历史保护；通用删除见上 | 新 [removeOriginalBatch:296–305][n-qc-batch-delete] 拒删当前、前批或有历史的批次 |
| 默认批切换 | 已核立即写入；恢复/事件触发有差异 | 原批窗 [129][o-qc-batch-switch]、管理窗 [419][o-qc-manager-switch] currentIndexChanged 即分步改当前和前批；原批窗 [193–196][o-qc-batch-promote] 读到只有前批时装载即提升为当前 | 新 [546/820][n-qc-batch-switch] 由 activated 发请求；[switchOriginalBatch:307–319][n-qc-store-switch] 事务更新，标志冲突拒绝而不自动修复，没有沿用原装载提升副作用 |
| 日期、名称等可编辑属性 | 确定差异，编辑字段清单仍需全量核 | 原批次 UI [readOnly:128/201/330][o-qc-batch-ui]，管理 UI 中名称厂家及若干数值字段设 readonly，未见 cpp 解除 | 新 [showManager:500–503][n-qc-editable] 主动解除只读；批次日期 [showBatchDialog:762][n-qc-batch-date] 可直接编辑。不要把新可编辑属性当成纯平台适配 |
| 历史查看与新增质控结果写入 | 查看基础已接；生产写入未接 | 原 [QualityDataTable::getQualityData:80][o-qc-history] 按品/项目/批读历史，另外由真实结果链写入 | 新 [recordOriginalResult:321][n-qc-result-write] 有后台实现，但当前全工程调用只找到声明、实现和测试；正式收包消费者没有调用。已有历史曲线/统计不能证明新 QC 结果已自动保存 |

## 4. 历史、患者、显示设置和导出弹窗覆盖矩阵

原明细患者字段本来就是先在界面输入、点 Save 才尝试写库；不是每个输入都立即生效。关键差异是关闭/切报告是否询问，以及 Save 失败后共享内存是否已变。

| 子流程 | 覆盖结论 | Windows 实际行为 | 当前正式工程及使用者差异 |
| --- | --- | --- | --- |
| 查询窗打开/输入/普通 Cancel | 已核；刷新时点不同 | [SetQueryCondition:60][o-report-query-init] 把父条件填进控件；直接 Cancel/X [74/139][o-report-query-cancel] 不主动复制控件值，但父 [on_btnQuery_clicked:1944–1952][o-report-query-parent] 不论取消均 refresh | 新 [openQuery:1233][n-report-query] 从 m_filter 填窗；取消 reject，不更新条件也不触发查询。普通“打开又取消”新不重查 |
| 查询 Save→无效→Cancel | **确定差异** | 原 [on_btnSave_clicked:79–134][o-report-query-save] 先把字段写父 QueryCondition，再校验日期/空条码；失败后 Cancel 仍可能留下改过的父条件，关窗后父重查 | 新 [openQuery:1277–1293][n-report-query-save] 用 next，验证通过才写 m_filter。没有原“无效 Save 已改父状态”的副作用 |
| 查询日期选择 | 静态基本路径已核；跨午夜待核 | 原 [on_btnCalendarStart/End_clicked:144/154][o-report-calendar] 弹原日历，选日更新控件，Save 后才进入查询条件；原 [Slot_ConfirmDate:1148][o-report-midnight] 另有自动重置条件再查链 | 新 [openQuery:1259–1274][n-report-calendar] QCalendar 选日只更新控件；跨午夜自动重置是否等价尚未核完。不能把手动日期选择已接扩大成全部日期切换已核 |
| 患者字段 Save | 已核；失败后内存结果有差异 | 原 [MaskDlgReportDetail::on_btnSave_clicked:622][o-report-patient-save] 先改共享 item_data，再 Update；失败提示不还原共享内存。Save 不关窗 | 新 [savePatient:1636][n-report-patient-save] 发后台；[HistoryStore::savePatient:882–925][n-report-patient-store] 成功后才采用持久化快照，失败保持未保存状态 |
| 明细 Cancel、top X | **确定差异** | 原 Cancel [构造:90][o-report-detail-cancel]、X [on_btnClose_clicked:556][o-report-detail-close] 直接关；父 [on_btnDetail_clicked:2003→2022][o-report-detail-parent] 关闭后无条件重读主表 | 新 [confirmLeave:849][n-report-leave] 对 X/关窗确认放弃；明确 Cancel [1478–1480][n-report-detail-cancel] 先回缓存再关；[finished:1483][n-report-detail-finished] 只 localResultsMode 自动 refresh，原数据库模式未沿原无条件主表重读 |
| 明细内部结果/患者等页签 | 已核，未发现自动写入差异 | 原 [561–578][o-report-detail-tabs] 只切 stack，同窗输入保留 | 新 [1470–1473][n-report-detail-tabs] 同样只切 stack。此处原有临时输入与新页相近 |
| 明细上/下报告、跨分页 | **确定差异** | 原 [on_btnUpRow_clicked:581 / on_btnNextRow_clicked:596][o-report-detail-nav] 直接调整 index/offset 并 Refresh，未保存输入被丢弃，没有统一确认 | 新 [moveDetailRow:1330][n-report-detail-nav] 先 confirmLeave；确认保留会阻止切行。跨页查回再采用已有异步链，但准确连续点击和失败后的页码位置待 RK 核 |
| 打印时患者有未保存输入 | **确定差异** | 原主表 [on_btnPrinter_clicked:1956][o-report-print] 和明细打印使用既有报告对象，未见 dirty 前置阻止 | 新 [printReport:836][n-report-print] dirty 直接拒绝，必须先保存或取消 |
| 显示设置：勾选/拖动→Save | 已核已有临时输入；存储失败语义有差异 | 原 [MaskDlgDisplaySetting 构造:37–59][o-display-save] Save 遍历两列表，按 display_name 改共享 header，逐条写库后直接关，未检查每条结果。普通勾选/拖动此前只在控件 | 新 [ReportRemainingDisplayDialog 构造:88–142][n-display-open] 改 m_columns，Save 发请求；[OriginalHistorySource::saveDisplay:216–258][n-display-store] 事务写库；[displaySaved:477–483][n-display-saved] 成功才采用并关窗，失败留窗可重试 |
| 显示设置：普通 Cancel/top X | 已核，原也不保存普通控件编辑；关闭后刷新有差异 | 原 [on_btnCancel/Close_clicked:181–188][o-display-cancel] 直接关；父 [on_btnDisplay_clicked:1313–1325][o-display-parent] 不论取消均重排原共享表头、刷新并重读报告 | 新 [构造:134–135][n-display-open] reject 丢 m_columns，不主动重读主表；直接关闭无本窗 dirty 提示，跨业务离开才由 [requestLeave:900–910][n-report-display-leave] 加放弃确认。不要把两者笼统写成“取消都不保存，所以一致” |
| **显示设置：恢复默认→Cancel** | **确定差异** | 原 [on_btnDefaultSort_clicked:191][o-display-default] 发信号；父 [Slot_DisplayDefaultSort:1346][o-display-default-parent] 调 get_table_header_from_db(true)，[get_table_header_from_db:472–514][o-report-header-write] 已插删更新表头并写默认 column_index，之后 Cancel 不撤数据库变化 | 新 [restoreDefaultOrder:247–268][n-display-default] 明确只改 draft，保留可见性，等 Save 成功才写。点默认后取消的新旧持久化结果不同 |
| 表头读取时配置维护 | **确定差异** | 原 [get_table_header_from_db:413–529][o-report-header] 读取时会删除不存在列、插入新列、按项目更新元数据；默认排序也借此写库 | 新 [OriginalHistorySource::display:188–213][n-display-read] 只读并保留旧表头；只有显式 saveDisplay 才插入/更新。不能把纯读取替代当作完全沿用原配置组织和读写结果 |
| 导出选择窗 Save/Cancel | 基本路径已核；Esc 机制待核 | 原 [MaskDlgExport 构造:25–26 / Save:64 / Cancel:58][o-export-dialog] 默认选“当前选中”；Cancel 设置 isCancel=true，Save 固定 isAll 后关；父 [on_btnExport_clicked:1332][o-export-parent] 非取消才开路径窗 | 新 [openExport:1666–1699][n-export-dialog] 同样默认选中，Cancel reject，Save 固定 all 后 accept 并开文件窗。原 isCancel 初值 false，Esc/系统关闭是否会误当确定需继续核 Mask，暂不作确定差异 |
| 文件路径窗选择/取消/覆盖 | **确定差异** | 原 [exportReport:1353–1415][o-export-file] 用原生 SaveFileDialog 任意选路径，空路径返回；QtCSV::Writer::REWRITE 允许标准确认后覆盖 | 新 [openExport:1684–1695][n-export-dialog] 非原生文件窗；[exportCsv:960–970][n-export-path] 只准本数据根 exports 新文件、不覆盖。取消无 fileSelected 不请求保存；可选位置与覆盖行为已改变 |
| CSV 内容、特殊字符 | 部分已核，精确原输出待核 | 原 [exportReport:1410–1412][o-export-codec] QtCSV GBK Writer；横/竖由不同取值路径整理，不能只看扩展名 | 新 [csvLine:223 / writeCsvLine:235][n-export-codec] 双引号转义、CRLF、GBK 不可表示字符即失败。原 Writer 对引号、换行、不可编码字符的最终字节尚未完整核，不能称一致 |
| PDF/HTML 选择与保存 | 已有正式处理；部分限制是新增 | 新报告控制器与原模板已接，见下一节；并非原报告 CSV 窗的同一入口 | [validateDestination:216][n-render-path] 限 exports 新文件；[validateHistoryHtmlSource:13][n-render-html] 遇 & 拒生成 HTML。拒绝是现行可见行为，原 renderer 未转义问题应保留说明，不凭“避免错误”直接认定业务等价 |

## 5. 报告模式、检测分组、算法与打印的边界

| 功能 | 原版活链 | 当前状态与结论 |
| --- | --- | --- |
| 横向/纵向显示 | [init_header:564][o-report-mode] 按 ReportTableType 选模式；原横表一份报告一行，纵表按任务/结果显示 | 新 [HistoryPage 构造:270–301][n-report-mode] 固定纵向。原资料模式接受配置为横向并不证明页面执行了横向。后续候选不作正式已接证据 |
| 横表显示后导出/打印的值来源 | 原 [display_results 横向分支:1025][o-report-cache] 改共享 item_values；选中导出/打印沿缓存；查询全量导出和重新读库是另一路 | 新直接按原库值重建视图，不能自动认作等价。须分别列显示、选中导出、全量导出、打印及重复测量；不应概括成“一律二次倍率”或“一律每种结果只换一次”。本轮未穷尽全部类型的字段与精度对照 |
| Simple / 非Simple 选择 | [getRackTasksCommand:3931–3951][o-task-protocol] 由 **SystemCommon::isNewProtocal** 决定，不是由操作系统决定 | 新检测会话的 linuxNew 参数和配置选择需沿配置实际来源核，不得拿 Linux 等同 Simple |
| 检测报告行分组 | 原非Simple [1663/1750–1755/2157][o-task-normal-group]，Simple [2343/2415–2435/2820][o-task-simple-group]；已发任务跳过、非 Idle/Reset 跳过，正 reportRowID 沿用；Simple 跨孔共行等顺序不同 | 新 [OriginalDetectionSession::tasksForRack:346–384][n-task-group] 仍统一构造 hole*1000+repeat 形式的临时报告组，不能称原“孔+重复次数”完全还原；大重复值还有碰撞边界待核。原协议分组、人工快捷新增报告、LIS 下载应用计划三项用途分别核 |
| 原曲线回看 | [getChartData:486→556][o-report-curve] 调 _AL4A_Get_Curve_UR_Dll，AD/OD/raw/nor/filter等不是仅画已有点即可等价 | 新 [openDetail:1440–1444][n-report-disabled] 相关控件仍禁用；本地原始散点不能替代供应商计算链 |
| QC 新结果落库、CAL 当前值更新 | 原真实接收/计算后分别保存业务结果 | QC 后台 recordOriginalResult 无正式消费者；CAL 已有资料和审核/绑定，不代表真实测量值→计算→当前曲线全部贯通；计算结果链单列待接，不扩写“校准全部未做” |
| 原主报告上传 vs 明细按钮 | 原主报告有真实 updateResult2Lis；但 [MaskDlgReportDetail::on_btnUpdateLis_clicked:615–619][o-report-debug-upload] 实际先发“无水无杯”临时动作并 return，后面的 SignalUpdateLisResult 不可达 | 主报告真正上传链待接；明细按钮不能按名字擅自恢复成上传，也不能当原有效上传漏移植 |
| 预览/实体打印 | 原 [QtReport::PrintReportData:30→175][o-print] 建原报告并 printExec(true) | 新 [HistoryReportController::preview:243][n-render-preview] 已建预览；[startNativePrint:436][n-native-print] 用固定快照启动独立确认。路径、取消、失败、提交时限和实际打印需要分别核；源码有处理不代表实际出纸通过 |
| 模板来源 | 原运行 CWD 下 resource/report_template/report.xml | 新 [documentFor:181–213][n-render-document] 原资料模式必须有原模板；[OriginalHistorySource::refresh:49][n-original-template] 读取模板。参考工程 root/bin 两份模板、原实际 CWD 未在本轮确定，列待核 |

## 6. LIS 设置、连接和业务收发覆盖矩阵

LIS 指医院检验信息系统。TCP 连接成功只是通道打开；查询任务、把患者和项目写入正式任务、发确认应答、上传结果并收到确认是后续不同步骤。

| 子流程 | 覆盖结论 | 原版依据 | 当前状态与差异 |
| --- | --- | --- | --- |
| 设置 Save | 已核；重复 Save 存在确定差异 | [frmLisSetting::on_btnSave_clicked:135–158][o-lis-save] 每次执行 setConfig，随后 signalConnectLis，不要求输入与上次不同 | 新 [frmLisSetting::on_btnSave_clicked:180][n-lis-save] !isDirty 直接返回；重复点 Save 不再次重连。Save 发后台，成功才更新 committed；字段保存成功不等于连接成功 |
| TCP/串口编辑与离页 | **确定差异** | 原页没有统一 dirty/离页守卫；保存时只将选中通信方式的字段写配置 | 新 [requestLeave:195–213][n-lis-leave] 对两种方式草稿一起确认，放弃回 committed。各模式字段组合、原验证器与 Store 最终写字段需继续逐键复核，不把“都有两种单选”作等价证据 |
| 首次自动连接/Save 后连接 | 已正式接 TCP，待真实业务验证 | [LisCommSetting::getAutoLink:113 / openComm:160][o-lis-open]；原首次启动及设置连接接线 | 新 [LisConnectionController::receiveCommitted:87 / openConfirmed:143][n-lis-controller] 首次按原 link，明确 Save 成功后开启；独立 settings/lis.ini 模式不伪接。此项不能继续写成“只有 configure 没调用” |
| 完整消息交接 | TCP 基础链已接，业务未接 | 原 [LisCommReceiver][o-lis-receiver] 继续解 DSR 并应用任务 | 新 [main.cpp:1473–1484][n-lis-not-applied] 接完整正文后只记 LIS_MESSAGE_NOT_APPLIED，明确 ack_sent=false、task_saved=false，并更新状态提示 |
| 查询字段、下载字段、应用计划、ACK 正文九源 | **正式构建已登记，生产未调用** | 原 [LisCommSetting:662 查询/763 ACK][o-lis-query] 和接收应用链 | 新 lis_business 九源已登记 [shell.pri:64–72][n-lis-pri]、[build.sh:268–276][n-lis-build]；应用计划只返回应赋字段/项目/顺序，不真正写患者、替换任务、报告 reserve 或发送。不能将纯字段函数称“完整下载已实现” |
| 真实等待/下载保存/确认发送/结果上传 | **未接** | 原 [normal ORU:258 / QC ORU:547][o-lis-upload]、QRY/DSR/ACK；原任务实际等待与接收消费者有调用链 | 当前 main 不调用这些九源生产处理；sendMessage 本身已有不代表 ORU/QRY 的调用。上传 ACK、重试/恢复、串口真实收发仍缺完整消费者 |
| Save/回读失败后的恢复 | 已有新保护，原行为一致性未关闭 | 原 setConfig/openComm 分别执行，不是当前这套带来源版本的停止状态机 | 新 [LisConnectionController::settingsError:212][n-lis-failure] 失败停止；同值重读不自动恢复，明确保存重开。该差异已列入当前计划，不能用“更安全”免除原行为对照 |

最新 [移植记录“LIS源码采用完成”:4259–4263][record-lis-current] 确认九源已正式采用、只准备字段和正文，未接生产查询、患者保存或实际发送，也未完成该批完整编译。较早 [4235][record-lis-old] 的“尚未纳正式”是历史时点，不应用作当前结论。

## 7. 数量、文字和队列限制：按来源及可达路径分类

仅发现一个常量，不能判它改变了有效业务。本节把已经在原库活路径执行的拒绝、仅非生产辅助函数中的预算，以及原本就有的限制分开。**不能把新增 local/offline 预算自动升级为用户认可的业务上限。**

| 分类 | 当前代码与有效路径 | 原依据和本轮结论 |
| --- | --- | --- |
| 原有上限，应保留 | [history_store.cpp:37][n-history-limits] 患者文字32767 | 原 QLineEdit 默认最大长度。不能和新增 CSV/任务预算混为一类 |
| 当前原库读取会执行的新增拒绝；有效原资料范围仍需确认 | [calibration_original_store.cpp:118–157][n-cal-read-limits] 曲线2048、点32、重复8、批组4096；ISI [353–357][n-cal-isi-limit] | 原校准库读取和构表动态循环，未见这些总量 LIMIT。2048/4096 是新读取门槛；**点数32/重复8是否已有上游项目配置限制，本轮未完成核实**。不能单凭常量称原可达操作必能超限 |
| 当前原库读取会执行的新增拒绝 | [qc_original_store.cpp:164/373–377][n-qc-read-limits] 控制品2048、历史1万、单条65536字符、总文字2MiB；[qc_history_core.h:14–17][n-qc-budget] 自称私有离线预算 | 原 [QualityDataTable::getQualityData:80–84][o-qc-history] 同条件 SELECT 无上述 LIMIT。当前已进入原库读取，不只是测试工具常量；原有效历史规模超过时会拒绝整个结果，实际允许业务规模待核 |
| 当前原库结果读取/导出会执行的新增拒绝 | [OriginalHistorySource::readResults:91–102][n-history-original-limit] 每报告64任务；[defaults:171–183][n-display-read] 结果列64；[history_store.cpp:38–39/1006][n-history-limits] CSV64MiB、100万报告 | 原 [ReportResultTable::queryDatasByFid:389–393][o-report-result-read] 和 QueryResultByFids 无64任务限制。不能据此宣称原当前UI一定可创建所有大样本，只能确认存量原库读取与导出的拒绝边界改变；须以有效原资料确认影响 |
| 当前报告活路径的新增格式限制 | [report_document.cpp:11/20–22/66–72/175/270/285][n-report-document-limits] 文字4096、64任务、384结果行、模板256KiB；[report_controller.cpp:53/104][n-render-timeout] 渲染/提交30秒观察时限 | build 的64任务、文字和384行校验均会用于原资料报告；fromJson 的384行校验也会被正式预览快照及独立渲染器读取快照使用，见下段。匿名配置分支里的64列条件不能直接算原库额外限制。原有效模板/字段及结果行是否实际超限仍需核，不以常量证明当前输入已受影响 |
| 当前 TCP 通道资源边界；不是业务字段规格 | [lis_tcp_transport.cpp:14–16/189/238/259–264][n-lis-buffers] 16对端、256收、64发、4MiB队列、1MiB帧；[lis_mllp_codec.h:15][n-lis-frame] | 当前连接可达，因此资源边界真实存在；但仍需原收发缓存实现、合法报文和有效并发规模，才能认定具体业务差异。不能仅凭本地注释推断原允许无限输入，或把这些值称仪器协议上限 |
| 已入构建但尚未生产调用的预算 | [original_lis_download_codec.h:8–13][n-lis-codec-budget] 正文1MiB、64段、128字段、4096项目；[application_plan.h:10–15][n-lis-plan-budget] 映射/任务4096、映射项16384、输入输出4MiB；[download_ack.h:8][n-lis-ack-budget] ACK4MiB | 当前 main 无生产消费者，**不能写成线上合法任务已经因此失败**。这是后续接入可能带入的新拒绝条件，尚缺原限制及用户认可的依据 |
| 原配置限制与非法结构校验 | 数据库唯一约束、原已有限长输入、对重复ID/无效ID/损坏结构的拒绝 | 应逐项追原 schema 和上游输入验证；不能把用于识别损坏资料的每个检查都认作新业务上限。例：质控 batch_name 的全库 UNIQUE 已见原 [quality_batch_table.cpp:16][o-qc-batch-unique]，新全库重名检查不能直接判为新增业务规则 |

**LIS 九份纯数据处理中，重复次数不是已展开的测试任务数。**[OriginalLisPlannedTask::repeats:78–79][n-lis-repeat-header] 保存的是 OBR-9（LIS 报文中的重复次数）经 int 转换后的一个标量；[buildOriginalLisApplicationPlan:215–216/249/274–276][n-lis-repeat-scalar] 小于1时取1，不截成99，只保存各项目的 repeats 和 scalarRepeatTotal，并检查总和能否用 qint64 表示，没有据此分配重复测试列表、任务号或报告行。因此“最多4096个计划项目”**不等于未来最多4096次实际测试**，也不证明实际重复任务展开、资源准入或失败恢复通过；真正展开和资源准入归后续尚未接入的任务消费者。本轮只确认标量准备方式，不能凭不存在的展开上限关闭资源核查。

**384行校验的实际接入位置：**[HistoryReportDocument::build:175][n-report-build-row-limit] 在生成报告行时拒绝超过 kMaximumRows=384，未用 originalProfile 排除原资料模式；[fromJson:285][n-report-json-row-limit] 在读快照时也拒绝超过384行，由 [report_render.cpp:29][n-report-render-snapshot] 的预览快照校验和 [report_renderer.cpp:104][n-report-renderer-read] 的独立渲染器读入调用。它们属于正式原报告路径会执行的校验；anonymousOptions:57 的匿名配置64列检查则属于另一个分支，不能混为原资料限制。是否存在能够到达并超过384行的有效原报告仍待核，不能仅凭常量推断实际业务损失。

## 8. 计划、记录、验证边界和仍待核实内容

| 文件或证据 | 当前可支持什么 | 不可扩大成什么 |
| --- | --- | --- |
| [移植计划:18，Windows行为优先][plan-principle] | 字段、原协议/机型/权限/状态和可见结果应先一致 | 不为本文新增草稿、保护或预算提供自动授权 |
| [当前分工卡][plan-current]、[LIS恢复差异条目:215][plan-lis-recovery] | TCP/消息交接与真实业务分开，配置失败恢复差异尚待核 | 不能把连接状态正常写成任务下载或结果上传完成 |
| [质控本地阶段卡][plan-qc] | 原历史本地 CRUD/图表已有记录，明确临床和设备另验 | 旧本地测试遵循新草稿预期，不能证明原保存/取消时点一致 |
| [知识指南:45–49][guide-current] | 包含页面/后台入口、历史证据范围；:47 已补TCP控制器新接线 | :15、:49 仍有“审核和应用未开放”等旧概括，与现校准源码冲突；应保留历史范围，更新当前说明，不反向认定功能尚缺 |
| [当前记录:4259–4267][record-current] | 九源已正式采用；已明确样本草稿设计改变原时点，旧测试不能证明原版一致 | 不把这份源码采用或旧脱机结果扩大为本报告所有操作已在RK通过 |
| [板B第10批有限运行证据][e-board10]、[第12批过渡待完成证据][e-transition12] | 记录各自版本/被动观察/关闭保护的具体范围 | 旧版本同主控连接、一次状态显示通过，不代表本次CAL/QC/报告/LIS功能通过，也不代表新版已启动 |
| [2026-11-02旧源审查][e-older-audit] | 可作遗漏索引和历史判断来源 | 本文以现工作区调用关系更新，不把旧“configure-only”等结论照搬 |

本轮**没有新增任何功能运行证据**。上述确定差异来自静态可达流程；需要运行才能确认的部分均登记待 RK 联机验证。遵守最新不再做脱机测试约定，不安排匿名资料、模拟服务器、本机回环或 Windows/WSL 本机运行替代验证。

仍待核实的事项：

- 原 Mask/CusDialogMask 的 Esc、系统关闭、动画关闭对 exec 返回及父函数继续执行的影响，尤其项目选择和导出选择窗；top X、明确 Save/Cancel 已按实际回调分别核。
- 原 QtCSV 对引号、嵌入换行、不可 GBK 编码字符及覆盖失败的最终输出；原横表缓存、全量导出、选中导出、打印逐结果类型的精度/单位/数值关系。
- 校准点数32/重复8是否为上游原项目编辑器已有限制；其他资源常量必须用可达有效输入确认影响，不凭技术缓冲数值定业务缺口。
- 跨午夜 Slot_ConfirmDate 的实际定时接线、新版是否具有等价触发；切查询日期、翻页、明细换行在异步失败后的最终选中行和条件。
- 原报告显示设置“默认排序后取消”的DB变化已确定，但原主表此刻是否立即采用新的排序，涉及父共享header和后续重读先后，不能只由DB已写推成全部即时显示结果。
- 原实际运行 CWD、两份 report.xml 的实际选择，原/新纸张、打印对话框默认值、正常确认及取消、提交异常、断连与实际出纸。
- 外部DB变动、快速连续点保存/取消/切换、关闭期间后台回调和失败恢复的全部可见结果。静态 busy 门闸已核，运行次序没有本轮证据。
- LIS 原各机型、协议和状态分支的完整生产接入仍未完成；查询等待、患者应用、任务替换、报告 reserve、ACK、上传确认及串口不能因九源已入构建而关闭。
- QC临床规则、CAL数值计算及新结果保存的真实链，须取得匹配算法/原资料并按顺序联机验证；先分清主控连接通过与真实仪器功能通过。

## 9. 计划更新建议（仅建议，本轮未改计划）

- 把“新增、编辑、Save、Cancel、top X、Esc、切记录、切页、无效 Save 后 Cancel、连续操作、写入失败后的内存与库”列成各页面子任务。原已有临时输入和新扩大的统一草稿分开登记。
- 将本文确定的操作差异列为待恢复原行为的独立条目；校准审核/批组应用已接，剩余是 dirty前置和数值链，不能整项重新写“校准未做”。
- 报告显示设置恢复默认、表头读库副作用、查询失败后父条件、患者保存失败后的共享内存分别列项；不把所有 Cancel 简化为“不写库即一致”。
- LIS“TCP连接/完整消息交接”“纯字段/应用计划/ACK正文已入构建”“生产查询/保存/发送未接”“串口/业务确认待做”分开维护。
- 为新增资源拒绝条件保留来源、适用模式、可达入口和有效输入依据；缺原规格或用户认可时明确待核，不将旧“离线预算”直接作为正式业务上限。
- 原空槽、调试早退、隐藏/未连入口独立注明，尤其明细上传调试替代、原未启用XLSX等；不要按按钮名称新造业务。

关键文件指纹（SHA-256，12:05:01 +08:00 读取）：

| 文件 | SHA-256 |
| --- | --- |
| [main.cpp][n-main-lis] | A38CFEC6C77CEC58FC546B2A775AB0B0FE7DBF471F7C0FA5464E78ED4FDDBC06 |
| [calibration_page.cpp][n-cal-save] | 3AEDDDA116D24AA917192116116BFD1FDA7635F82DA91FCD572E486006AEDE4F |
| [quality_control_page.cpp][n-qc-save] | 29567028E167FD29875F803D6134891E9E0ACD313A9EA788E24E1817CF2B0FAE |
| [history_page.cpp][n-report-query] | 61B6BA88FB32F119C043901C6608631B8EA56884051202764A48D4A9C0676745 |
| [lis_connection_controller.cpp][n-lis-controller] | 7CA09F58D332AC20BF0F7D234C7FC4081A5ACDDADF646484BBA11D81F69C7B61 |


[n-main-cal]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/shell/main.cpp:1094
[n-main-qc]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/shell/main.cpp:1017
[n-main-lis]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/shell/main.cpp:1451
[n-lis-not-applied]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/shell/main.cpp:1473
[n-cal-shell]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/shell/calibration_shell.cpp:11
[n-qc-shell]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/shell/quality_shell.cpp:52
[o-cal-select]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frmcalibration/frmcalibrationmaterial.cpp:329
[o-cal-click]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frmcalibration/frmcalibrationmaterial.cpp:193
[o-cal-save]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frmcalibration/frmcalibrationmaterial.cpp:1299
[o-cal-update]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frmcalibration/frmcalibrationmaterial.cpp:1246
[o-cal-copy]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frmcalibration/frmcalibrationmaterial.cpp:1311
[o-cal-delete]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frmcalibration/frmcalibrationmaterial.cpp:1328
[o-cal-review]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frmcalibration/frmcalibrationmaterial.cpp:1385
[o-cal-apply]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frmcalibration/frmcalibrationmaterial.cpp:996
[o-cal-default]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frmcalibration/frmcalibrationmaterial.cpp:1052
[o-cal-checked]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frmcalibration/frmcalibrationmaterial.cpp:476
[o-cal-cancel]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frmcalibration/frmcalibrationmaterial.cpp:41
[o-cal-method-parent]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frmcalibration/frmcalibrationmaterial.cpp:1442
[o-cal-recalc]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frmcalibration/frmcalibrationmaterial.cpp:1181
[o-cal-chart]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frmcalibration/frmcalibrationmaterial.cpp:1107
[n-cal-select]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/calibration/calibration_page.cpp:120
[n-cal-leave]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/calibration/calibration_page.cpp:751
[n-cal-save]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/calibration/calibration_page.cpp:619
[n-cal-copy]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/calibration/calibration_page.cpp:634
[n-cal-actions]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/calibration/calibration_page.cpp:278
[n-cal-delete]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/calibration/calibration_page.cpp:643
[n-cal-review]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/calibration/calibration_page.cpp:663
[n-cal-apply]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/calibration/calibration_page.cpp:674
[n-cal-cancel]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/calibration/calibration_page.cpp:656
[n-cal-close]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/calibration/calibration_page.cpp:770
[n-cal-new]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/calibration/calibration_page.cpp:466
[n-cal-method]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/calibration/calibration_page.cpp:733
[n-cal-raw]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/calibration/calibration_page.cpp:760
[n-cal-store-save]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/calibration/calibration_original_store.cpp:184
[n-cal-read-limits]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/calibration/calibration_original_store.cpp:114
[n-cal-isi-limit]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/calibration/calibration_original_store.cpp:347
[n-cal-rights]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/calibration/calibration_store.cpp:94
[o-cal-method-save]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frmcalibration/maskdlgcalculateidsetting.cpp:65
[o-cal-method-cancel]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frmcalibration/maskdlgcalculateidsetting.cpp:60
[o-qc-add]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_quality/mask_dlg_add_control.cpp:324
[o-qc-save]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_quality/mask_dlg_add_control.cpp:364
[o-qc-close]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_quality/mask_dlg_add_control.cpp:84
[o-qc-used]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_quality/mask_dlg_add_control.cpp:501
[o-qc-select]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_quality/mask_dlg_add_control.cpp:121
[o-qc-projects]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_quality/mask_dlg_add_control.cpp:159
[o-qc-delete]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_quality/mask_dlg_add_control.cpp:352
[o-qc-manager-switch]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_quality/mask_dlg_add_control.cpp:419
[n-qc-add]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/quality_control/quality_control_page.cpp:676
[n-qc-success]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/quality_control/quality_control_page.cpp:219
[n-qc-save]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/quality_control/quality_control_page.cpp:664
[n-qc-leave]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/quality_control/quality_control_page.cpp:464
[n-qc-manager-close]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/quality_control/quality_control_page.cpp:507
[n-qc-used]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/quality_control/quality_control_page.cpp:641
[n-qc-select]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/quality_control/quality_control_page.cpp:517
[n-qc-rule-select]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/quality_control/quality_control_page.cpp:529
[n-qc-project-save]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/quality_control/quality_control_page.cpp:731
[n-qc-batch-add]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/quality_control/quality_control_page.cpp:767
[n-qc-batch-edit]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/quality_control/quality_control_page.cpp:787
[n-qc-batch-switch]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/quality_control/quality_control_page.cpp:820
[n-qc-editable]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/quality_control/quality_control_page.cpp:500
[n-qc-batch-date]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/quality_control/quality_control_page.cpp:762
[n-qc-store-save]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/quality_control/qc_original_store.cpp:172
[n-qc-delete]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/quality_control/qc_original_store.cpp:207
[n-qc-batch-delete]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/quality_control/qc_original_store.cpp:296
[n-qc-store-switch]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/quality_control/qc_original_store.cpp:307
[n-qc-result-write]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/quality_control/qc_original_store.cpp:321
[n-qc-read-limits]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/quality_control/qc_original_store.cpp:164
[o-qc-project-close]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_quality/quality_control_item_add_project_wgt.cpp:16
[o-qc-project-flag]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_quality/quality_control_item_add_project_wgt.h:30
[o-dal-delete]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/tool/database/table_dal.cpp:390
[o-qc-batch-add]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_quality/mask_dlg_control_batch_setting.cpp:52
[o-qc-batch-delete]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_quality/mask_dlg_control_batch_setting.cpp:95
[o-qc-batch-switch]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_quality/mask_dlg_control_batch_setting.cpp:129
[o-qc-batch-promote]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_quality/mask_dlg_control_batch_setting.cpp:193
[o-qc-batch-ui]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_quality/mask_dlg_control_batch_setting.ui:128
[o-qc-history]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/tool/database/quality/quality_data_table.cpp:80
[o-qc-batch-unique]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/tool/database/quality/quality_batch_table.cpp:16
[n-qc-budget]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/quality_control/qc_history_core.h:14
[o-report-query-init]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_report/mask_dlg_query_report.cpp:60
[o-report-query-cancel]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_report/mask_dlg_query_report.cpp:74
[o-report-query-save]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_report/mask_dlg_query_report.cpp:79
[o-report-calendar]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_report/mask_dlg_query_report.cpp:144
[o-report-query-parent]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_report/frm_report_material.cpp:1944
[o-report-midnight]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_report/frm_report_material.cpp:1148
[o-report-detail-parent]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_report/frm_report_material.cpp:2003
[o-report-print]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_report/frm_report_material.cpp:1956
[o-display-parent]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_report/frm_report_material.cpp:1313
[o-display-default-parent]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_report/frm_report_material.cpp:1346
[o-report-header-write]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_report/frm_report_material.cpp:472
[o-report-header]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_report/frm_report_material.cpp:413
[o-export-parent]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_report/frm_report_material.cpp:1332
[o-export-file]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_report/frm_report_material.cpp:1353
[o-export-codec]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_report/frm_report_material.cpp:1410
[o-report-mode]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_report/frm_report_material.cpp:564
[o-report-cache]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_report/frm_report_material.cpp:1025
[n-report-query]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/history_page.cpp:1233
[n-report-query-save]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/history_page.cpp:1277
[n-report-calendar]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/history_page.cpp:1259
[n-report-patient-save]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/history_page.cpp:1636
[n-report-leave]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/history_page.cpp:849
[n-report-detail-cancel]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/history_page.cpp:1478
[n-report-detail-finished]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/history_page.cpp:1483
[n-report-detail-tabs]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/history_page.cpp:1470
[n-report-detail-nav]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/history_page.cpp:1330
[n-report-print]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/history_page.cpp:836
[n-display-saved]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/history_page.cpp:477
[n-report-display-leave]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/history_page.cpp:900
[n-export-dialog]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/history_page.cpp:1666
[n-report-mode]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/history_page.cpp:270
[n-report-disabled]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/history_page.cpp:1440
[o-report-patient-save]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_report/mask_dlg_report_detail.cpp:622
[o-report-detail-cancel]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_report/mask_dlg_report_detail.cpp:90
[o-report-detail-close]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_report/mask_dlg_report_detail.cpp:556
[o-report-detail-tabs]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_report/mask_dlg_report_detail.cpp:561
[o-report-detail-nav]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_report/mask_dlg_report_detail.cpp:581
[o-report-debug-upload]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_report/mask_dlg_report_detail.cpp:615
[n-report-patient-store]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/history_store.cpp:882
[n-export-path]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/history_store.cpp:960
[n-export-codec]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/history_store.cpp:223
[n-history-limits]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/history_store.cpp:35
[o-display-save]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_report/mask_dlg_display_setting.cpp:37
[o-display-cancel]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_report/mask_dlg_display_setting.cpp:181
[o-display-default]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_report/mask_dlg_display_setting.cpp:191
[n-display-open]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/report_remaining_display_dialog.cpp:88
[n-display-default]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/report_remaining_display_dialog.cpp:247
[n-display-store]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/original_history_source.cpp:216
[n-display-read]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/original_history_source.cpp:171
[n-original-template]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/original_history_source.cpp:49
[n-history-original-limit]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/original_history_source.cpp:91
[o-export-dialog]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_report/mask_dlg_export.cpp:25
[n-render-path]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/report_controller.cpp:216
[n-render-preview]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/report_controller.cpp:243
[n-native-print]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/report_controller.cpp:436
[n-render-document]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/report_controller.cpp:181
[n-render-timeout]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/report_controller.cpp:53
[n-render-html]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/report_remaining_html.h:13
[o-task-protocol]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frmsample/frmsamplematerial.cpp:3931
[o-task-normal-group]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frmsample/frmsamplematerial.cpp:1663
[o-task-simple-group]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frmsample/frmsamplematerial.cpp:2343
[n-task-group]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/samples/original_detection_session.cpp:346
[o-report-curve]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frm_report/report_string_pasher.cpp:486
[o-print]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/tool/qtrpt_module/qtreport.cpp:30
[o-lis-save]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/frmuserset/frmlissetting.cpp:135
[n-lis-save]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/ui/frmlissetting.cpp:180
[n-lis-leave]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/ui/frmlissetting.cpp:195
[o-lis-open]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/commlis/liscommsetting.cpp:113
[o-lis-query]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/commlis/liscommsetting.cpp:662
[o-lis-upload]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/commlis/liscommsetting.cpp:258
[o-lis-receiver]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/commlis/liscommreceiver.cpp:248
[n-lis-controller]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/lis_transport/lis_connection_controller.cpp:87
[n-lis-failure]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/lis_transport/lis_connection_controller.cpp:212
[n-lis-pri]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/shell/shell.pri:64
[n-lis-build]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/shell/build.sh:268
[o-report-result-read]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/tool/database/report/report_result_table.cpp:389
[n-report-document-limits]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/report_document.cpp:20
[n-lis-buffers]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/lis_transport/lis_tcp_transport.cpp:14
[n-lis-frame]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/lis_transport/lis_mllp_codec.h:15
[n-lis-codec-budget]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/lis_business/original_lis_download_codec.h:8
[n-lis-plan-budget]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/lis_business/original_lis_application_plan.h:10
[n-lis-ack-budget]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/lis_business/original_lis_download_ack.h:8
[record-lis-current]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/02_records/20261022_sn100_host_migration.md:4259
[record-lis-old]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/02_records/20261022_sn100_host_migration.md:4235
[record-current]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/02_records/20261022_sn100_host_migration.md:4259
[plan-principle]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/01_docs/plans/sn100_host_migration_plan.md:18
[plan-current]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/01_docs/plans/sn100_host_migration_plan.md:102
[plan-lis-recovery]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/01_docs/plans/sn100_host_migration_plan.md:215
[plan-qc]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/01_docs/plans/sn100_host_migration_plan.md:795
[guide-current]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/01_docs/knowledge/sn100_host_project_guide.md:45
[e-board10]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/04_references/sn100_host/board_b/20261103_integration_10_keep_running/README.md:1
[e-transition12]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/04_references/sn100_host/board_b/20261104_integration_12_transition_pending/README.md:1
[e-older-audit]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/04_references/sn100_host/source_audit_20261102/README.md:1
[n-lis-repeat-header]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/lis_business/original_lis_application_plan.h:78
[n-lis-repeat-scalar]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/lis_business/original_lis_application_plan.cpp:215
[n-report-build-row-limit]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/report_document.cpp:175
[n-report-json-row-limit]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/report_document.cpp:285
[n-report-render-snapshot]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/report_render.cpp:29
[n-report-renderer-read]: C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/history/report_renderer.cpp:104

