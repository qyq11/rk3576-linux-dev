# 主页面接口需求与原版核查（临时协调）

本窗口只改 project_page.h/.cpp。当前接口已接入，ARM64源码语法检查已通过，尚未运行电脑或RK版本。是否可整合以handoff.json的codeReady和SHA为准。

## 原版依据

- frmsystemmaterial.cpp:2471起保存 xishu_a、xishu_b、sample_warmup_length；279–281限制为-9999至9999、4位小数，sample_warmup_length实际是系数C，不能按字段名解释成预热时间。
- 2234从collect_rate_ms显示采样值，320设只读；分析方法228原本就禁用，本次继续保持。
- 1723–1750及2404起：四个算法启用项对应projectResult中Calibration/Rate/INR/MDA的is_used，控制是否显示结果行。与主显示display不同，不能混成一个勾选。
- 2840起复制、2965起删除；固定项目不可删除。当前正式canEdit要求已认证、actionReady、角色4或5。

## 请数据窗口明确

1. create(requestId, ProjectRecord)以及单位增加/修改/删除的稳定槽名、参数与operation回执名。
2. 算法系数及四个is_used的ProjectRecord字段，保持结果启用与mainResult的语义独立；旧库值未知的处理不能默认宣布已配置。
3. 新建项目初始允许的结果类型/单位/稀释项约束；导入入口的具体格式和数据槽由数据窗口定义。本页可以提供人工新建和单位维护先完成空库流程。
4. 保存不可再用旧sampleRates覆盖稀释编辑器的新值。

## 请稀释窗口明确

提供方案取值接口，至少包含标签、可选、默认、顺序；本页会把新值送入ProjectRecord.sampleRates，advanced继续使用advanced()。

## 本页拟定入口

沿用原项目列表的操作区域，右键菜单及原侧栏btnChange位置的可见“项目管理…”按钮提供新建项目、导入配置、单位维护、重新读取；不改.ui或重排原业务区。必要差异原因：原版只有复制，空库无法开始维护；增加真实入口填补初始化缺口。新项目先在页面编辑草稿，点击原保存后等待真实create回执；取消恢复已提交项目。

## RK驱动可用的实际入口

- 点击btnChange打开QMenu：createProject / importProjectConfiguration / manageProjectUnits / reloadProjects（各QAction的objectName）。项目表lstProject右键也可打开同一菜单。
- createProjectDialog：newProjectName、newProjectFullName、newInstrumentProjectId三个QLineEdit；newProjectResults为4行4列表，依次Original/Calibration/Rate/INR，列0类型只读，列1单位QComboBox（须明确选择），列2启用QCheckBox，列3显示QCheckBox。初始仅Original启用/显示是可见的初始建议，进入编辑时由使用者明确确认；结果范围仍须按有效资料核对。editNewProject点击后只产生页面草稿，原btnSave才提交create。原btnCancel取消新建而不落库。
- projectUnitDialog：projectUnitsTable列表、projectUnitName输入框、projectUnitRatio整数框；addProjectUnit / updateProjectUnit / removeProjectUnit / reloadProjectUnits按钮。写入期间禁止关闭和重复提交；回执后重新load，失败保留输入。
- projectImportFileDialog：显式Qt QFileDialog，选择.json，不读取参考数据库；输入格式按data/contract.md。确定后等待importFile回执。可用QFileDialog的fileNameEdit和buttonBox操作实际文件选择。
- 原project_en_name/project_cn_name/project_lis_code、xishu_a/xishu_b/sample_warmup_length和is_used继续原objectName；三个系数全空表示尚未配置，部分空拒绝。ckxCalibrationUsed/ckxRUsed/ckxINRUsed各保存对应used，不改变已确认display。旧未知项首次点击会另询问display状态，取消保持未知。隐藏MDA和原分析方法继续关闭。
- 样本方案直接调用稀释窗口的sampleRateLabels/sampleSelections/defaultSampleLabel/validateSampleSelections，diluentOptionsReady已连接setDiluentChoices；不再保留旧sampleRates覆盖新值的页面逻辑。

## 页面状态保护

save/create/copy/remove/import/单位操作都按请求号和operation匹配真实回执，过期回复不覆盖当前草稿。写成功后load失败时禁止继续用旧快照编辑，从项目管理重新读取恢复。关闭/离页时，写入期间等待、弹窗期间拒绝离开、草稿询问放弃；取消只恢复快照。权限仍由Store.canEdit双重检查，访客可查看单位但无写按钮。

## 仍需整合执行

本窗口只做源码审查和ARM64源码检查，不运行任何电脑版本、不部署/操作RK。待验：空库从实际菜单建单位与项目、全部按钮/弹窗、三系数保存重开及非法输入、复制独立选择、固定/引用拒删、只读角色、失败重试、取消/离页/关闭、过期回执、结果格式弹窗与启用值一致、RK资源和原版视觉。

