# 校准与质控任务归属交接

本窗：`01a19c13-ad45-72c1-a5fe-736a6660a836`，“SN100 校准与质控任务归属”。交给总协调窗`01a19c11-0cd5-7143-86a1-adaa47ced125`整合。

**本次交付是临时候选代码，不是已发布功能。**只写temp，保留根目录所有已有修改；没有应用正式补丁、部署、替换板端程序、连接RK或占用RK显示，也没有提交或推送Git。根README、计划和统一迁移记录由总窗口维护。

`candidate/`是累计候选；`candidate_patch.diff`及`patches/`是相对冻结时正式源码的差异，`source_manifest.json`保存底稿和候选指纹。`base_current/`仅供逐段核对，不能整包回退。**第二片是唯一最终应用输入，不要先应用第一片再叠加。**第一片`../qc_calibration/`保留准备过程及Windows证据，SQL错误已同步修正；它未包含最终任务分流、输入正则和点排序修正。

## 原来已有、部分接通及本次候选

| 范围 | 开始时的实际情况 | 本次候选变化 |
| --- | --- | --- |
| 校准资料、审核、批组绑定 | 原库和页面已经接入，不能报成全缺；不同权限分支尚待原版一致性核对 | 不重做这些功能；不自动审核或应用，不把编号/参数/绑定当算法结果 |
| 质控控制品、批次及关联项目 | 原资料页已有；早期本地样本预设也有UUID引用保护，但原资料模式没有装配该桥接 | 原样本工作线程直接读取原quality.db的品/批次/关联项目，保存与下发前再核，原模式不冒用本地catalog UUID协议 |
| 样本/任务 | 普通任务、试剂分配和待计算曲线已有；仓库、会话直接拒绝非普通样本；校准按钮禁用 | 保留原0普通/1校准/2质控身份；恢复原校准项目列表、设置弹窗和数字键盘；分三类准备/占号/下发/接收 |
| 设备错误、恢复和重复 | 同步窗口第一片刚提供TR恢复、阶段8用量标志、断线保守规则 | 完整保留其机制；合总窗sync_review_fixes的重复Tr登记和TR0空TEXT修正；CAL不造普通报告行 |
| 计算及真实结果 | 缺完整计算链 | 仍不实现；收到曲线后保存“待计算”，不拟合、反算、ISI/INR或质控判定，不向quality_Data.db伪造result_val |

## 从哪里读代码

| 入口/文件 | 关键规则和原版函数 |
| --- | --- |
| `samples/sample_page.cpp`、`sample_calibration_page.cpp`及原`.ui`/键盘 | 原`frmSampleSettingMaterial::on_btnCalProj_clicked/slotBtnCalProjClick/slotChangeCalTest`、`MaskDlgCalibratorSetting::on_btnSave_clicked`：项目校准结果启用才出现；空孔设置；自动比例从小到大、手动连续孔；`c-批号`为预设身份；取消不改架草稿 |
| `original_calibration_preset.*`、`sample_types.h` | 独立点号、原浓度文字、各点比例/孔位/重复数；同组UUID仅关联草稿，不是原校准编号、任务号或算法状态。单位换算及QString默认精度沿原设置代码 |
| `original_sample_repository.*`、`sample_qc_preset.*` | 原`MaskDlgSampleDetail::on_btnSave_clicked`的控制品/批次及项目；保存拒绝失效身份、不同项目/批号、缺点或重复点。`last_cal_para`按原project_base的name/value键值表更新，不另造列或表；文件指纹/锁/事务失败保留输入 |
| `original_task_codec.*`、`original_detection_session.*`、`original_control_tasks.*` | 原`getRackCalibrationTasksCommand[Simple]`、`getRackNormalTasksCommand[Simple]`、`slotSendRackTasks`：先CAL后SAM/QC；LinuxNew由设备标识决定。普通任务号仍用results.db计数；CAL另用原拼写`calibraiton_total_count`，点表only_count按原任务顺序用`|`连接；插入按数字校准号/点号排序，避免10排到2前 |
| `original_sample_store.cpp`、`sample_store.h` | 原资料选择读取、上次浓度保存、任务准备/实际应答/状态变化均在样本线程；新`originalCalibrationRecordsChanged`通知校准页重读 |
| `shell/main.cpp`、`shell/build.sh`、`samples_core.pri` | 只加校准刷新及新增源/表单/Qt信号生成接线。原资料下不启用早期`connectSampleQuality`；通过原Store/Page专用选择信号接入。主窗有未保存校准草稿时不覆盖刷新 |

## 身份、报文及保存结果

- 原`S_SampleType`质控为2，但原`database_com.h::ReportType::kQualityData`是1。候选QC使用`report_data.type=1`，没有把两个枚举混用。原接口草表曾误写2，已纠正，错误值未进候选SQL。
- QC任务携带原控制品ID、批次ID、项目关联ID及名称快照。设备条码与“控制品-批次”预设号分开；回包先按唯一taskId，再核架/孔/类型/项目/样本/比例/试剂组，不能按同名或当前选中行猜归属。原`quality/controlName`、`quality/batchGroup`键保留，额外ID写`rk_quality`追查区。
- CAL占原独立校准编号，`calibration_base`保存原项目/校准批号/点数/重复数/单位/原变换参数，`is_checked/is_edited=0`；`calibration_data`保存点号、比例、浓度及各次设备任务号，不占普通report_data/report_results。回包只更新正确点的非数值状态“待计算”（原未知标志5），不写0或测量值；已有数字、编辑结果或审核记录拒绝覆盖。
- 原CAL手动链的sampleCode初值为空；`c-批号`仅是sampleID，候选没有把它伪作扫码条码。原LIS自动索取可另行填条码，该分支不在本次接通范围，须实际核对后再扩。
- CAL旧协议非Default且有关联时按原`batchGroup.contains(reagentBatch)`筛瓶；Default按优先级选全部可用瓶。原LinuxNew校准不按批组筛瓶，仍发送所选组和逐步实际孔名。CAL两协议的稀释液孔字段实际发Cleaner，不能直接套普通IBS孔。未新增线上校准编号字段、未改编码/字段顺序。
- 原曲线文件：CAL为`result/calibration/<taskId>`；新QC沿原`saveResultInformation`最终写入`result/yyyy-MM/<taskId>`。SAM已经接通的日目录保留。原`saveSampleInformation`预占位使用日目录，而最终结果保存使用月份目录；这两条源码和本次Linux新QC路径有明确记录，但Windows完整文件变化未实跑。
- 相同Tr完整成功后仅确认旧结果，不再按位置改新预设；不同原始内容报冲突。文件先落盘、数据库暂失败时仍保留原始证据，重试同包可补事务。TD9不释放尚无Tr的任务保护；旧连接未知任务不推测恢复。TR只退未加样的原实际预留量；CAL沿原无独立TR点表更新分支，只核该点占号身份并改孔状态/退量，不伪造普通报告或算法结果。

## 实际验证及失败记录

**源码检查与ARM64对象：**第二片11份C++源码及3份Qt信号对象已在独立目录交叉编译；新会话头布局修改后重编全部候选，随后按修正重编点排序、输入正则等受影响对象。最终状态及SHA见`arm64_compile/compile.log`、`exit_code.txt`、`object_manifest.json`。Qt缓存1331份所需头文件与工程sysroot逐字节指纹一致。已有样本聚合初始化有缺省成员警告，成员均保留默认值。这里只是对象编译；公共main/build窄补丁尚待总窗口完整链接，未产出本窗正式可部署程序。

**没有做脱机功能测试：**不运行Windows/WSL移植版、匿名数据、模拟服务器或回环，不做Windows测试构建。源码检查不能代替SQL运行或RK验证。

**保留失败：**第一轮候选漏复制本地协议头，混到旧Snapshot导致新增字段不可见，已补隔离头；Qt信号生成脚本误传C++编译选项，已修并生成编译成功。总窗审查发现上次浓度参数SQL误把键值表当宽表，已在两片修为`UPDATE project_base SET value=? WHERE name='last_cal_para'`并重编。点分组字符串排序会使≥10点物理顺序不同，已改数值二元键。上述前两项是编译失败，后两项是源码复查发现，没有在运行数据库上试错，也不能把修前编译通过说成SQL可用。

**Windows实际运行：**19:56至19:59以及候选完成后20:21至20:22，均在已有D盘发行版实际进入/取消同功能。PT Default禁选、104%、1/4/1/2/1/1及26/52/104可见；空批号确定确报“校准品批号为空!”，关闭提示保留原输入，取消/重开及返回空任务已实见。质控切换启用两下拉并禁用普通样本号，但当前无可选控制品/批次；未造资料或按开始。详见`../qc_calibration/windows_review/README.md`。数据库被程序占用，未取得其前后指纹，不能报告数据库不变；本次未提交有效保存。原EXE与参考源码版本完全一致仍未确认，当前画面SN450，不能当SN500或RK验证。

## 仍需核对及总窗接续

1. 先核板B身份、实际INI和当前42配置/设备48的孔位映射；本窗不解除总窗的开始阻断。只有本次明确允许的联机动作才能验证。以有效原控制品/批次、校准项目和试剂核选择→保存→Tt请求→发出具体任务→TD/Tr/TR→原报告/点表/原始文件及正常关闭重开，保留真实字节和数据变化。
2. 候选的写锁、外部修改、同包重试、不同包、混类型/错点/错批号、跨午夜、断线和进程重开在途恢复没有RK结果。当前仍仅本进程issuedTasks识别任务，重开不从报告猜旧回包归属。资源、实屏/键鼠、原版视觉、长稳及同板控制均待总窗。
3. 原手动浓度框无数值validator，原保存的toDouble对非数字会变0；候选明确拒绝非数字、负值和非有限值，此保守差异列待确认，不能称完整原版校验一致。自动输入已沿getExpStr实际正则，禁指数写法；原注释“0-999”与实际三位整数/三位小数规则不同。原批号正则精确16位、保存却只判非空，不能把Qt中间态当已确认有效批号。
4. 原手动点孔位溢出、重复次数、旧协议无匹配试剂瓶的替代动作仍待明确；候选不截断点、不默改重复数、不降级自动选别组。当前校准资料页已有32点/8重复显示上限，而原任务配置更大时的联机和显示不能据此宣布支持完整。
5. QC收到原始曲线后，正确报告和稳定品/批次/项目身份已保存，**quality_Data.db的数值历史还要等后续算法产生真实结果**。已有recordOriginalResult接口并非缺失；不以空包或0占“判定完成”。原可读`quality_rule_algth.cpp`留到算法阶段，不统称闭源资料缺失。
6. 校准审核和绑定已经存在；原Engineer门槛/未审核询问与当前power1分支的差异不在本轮修改，单列原版一致性后续核对。新编号、only_count、曲线原始点及is_checked均不证明拟合/反算/ISI/INR或质控判定生效。
7. 应用前校验source_manifest底稿；共享main、build及样本Store/仓库只按窄补丁串行合并，不能整文件覆盖其他窗口。同步窗口固定候选、sync_review_fixes的结果保护及本候选须同时保留。总窗完整编译、依赖核对、正式部署和实际仪器结果分别记录；本窗到此交接，不扩展算法或其他模块。
