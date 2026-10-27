# 稀释方案接入约定（2026-10-27）

当前状态：接口已确定，正在实现；正式源码只改 project_advanced.h/.cpp。这里是临时接入说明，正式结论由RK整合窗口写回原记录。

ProjectAdvancedEditor 不包含 projects_store.h，避免循环包含。原 showAdvanced(value, rateLabels)、setSampleSelections(options, defaultLabel)、advanced() 保留。主页面加载/取消时依次调用 showAdvanced(project.advanced, 已提交方案名称列表)、setSampleSelections(已提交可选状态映射, 已提交默认名称)，恢复全部状态。

新增只读取值接口：
- QStringList sampleRateLabels() const：表格实际顺序，包含新增加的行；名称草稿原样返回，不能静默删行。
- QMap<QString, bool> sampleSelections() const：名称对应选用状态。保存前必须先检查 validateSampleSelections()，避免重复名称压缩映射。
- QString defaultSampleLabel() const：当前默认名称；未选返回空。
- QString validateSampleSelections() const：空名称、非法名称、重复名、没有方案、没有可选方案、默认不存在/不可选等返回中文错误。有效返回空。
- void setDiluentChoices(const QStringList &names)：仅传已提交且有效的“稀释液”目录，不能用普通试剂/清洗液充数。下拉框保留 none 和过期已存值，过期值保存时由Store拒绝。

主页面保存：先校验，逐个 sampleRateLabels() 生成 ProjectSampleRate{名称, options.value(名称), 名称==defaultSampleLabel(), 1起顺序}；赋给 project.sampleRates；project.advanced=advanced()。

稀释液保存字段确定为 ProjectAdvanced.fields["IBS_name"]（原版同名控件/字段）。加入 scalarFields()；旧资料无此键按 none 展示，不能猜稀释液。Store须在有缓冲液量的方案中要求有效稀释液，并复核名称类型/状态及引用保护。新增 ProjectRecord 独立稀释液字段没有必要，若数据窗口已有兼容限制请在 data/contract.md 写明。

advanced() 返回全部表格行，含新行与非法草稿，不再按旧 rateLabels 过滤。samples 的 sample_rate 与 sampleRates 一一对应；sample_option 为0/1。默认仅存在 ProjectRecord.sampleRates.defaultOption，不额外写到动作JSON。

Store必须移除 save() 内 project.sampleRates=found->sampleRates 的旧覆盖，并保留唯一默认、默认必须选用、顺序/重名校验。仅做高级动作标量保存但无动作行的旧配置继续兼容；新增空行含未填参数，保存必须报错而非将空串转0。

原版依据：addSampleRateTableDate()生成可编辑名称和可选复选框；on_btnAddSampleRate_clicked()增加空行，on_btnDelSampleRate_clicked()删除当前行；读取时仅从sample_option=1的方案构建默认列表；updateSampleRateTable()以名称标记默认，原版缺默认时会选第一行。移植版对失去默认的草稿明确要求重新选择，避免无提示改变默认；最后一行可在草稿中删除，保存拒绝缺方案，可取消或新增恢复。该差异是完整性保护，不新增临床数值。

待RK：按钮增删、名称/选用/默认、稀释液目录、非法模式/体积、保存重开和取消恢复；原版布局及颜色保持.ui不变。开发窗口不运行本机版本、不部署/运行RK。

## 第一可接入切片已完成（codeReady=true，待ARM64构建/RK）

上述接口均已在 project_advanced.h/.cpp 实现。数据接口第一版已读取，字段与之对齐；setDiluentChoices 接收 data窗口新增 diluentOptionsReady 返回的“缓冲稀释液”。主页面按上述取值逐行组成ProjectSampleRates；取值之前必须调用 validateSampleSelections，避免重名映射丢状态。

更正空方案边界：与数据窗口一致，零方案表示“尚未配置”，允许草稿删除最后一行后成为空列表；不宣称其可用于检测。只要列表非空，必须有唯一且选用的默认。删除/取消选用默认时清除默认并要求重新选择，不静默切到第一行。单个下拉框本身只产生一个默认，多个默认由Store对外来数据拒绝。

原版default小表本轮恢复编辑，直接同步同一方案的真实行，不另存重复动作。镜像名称只读（原版对应列也只读），体积/模式/混匀/预清洗次数可改；行删除时不残留按旧行号修改其他方案的回调。none/onedilute反应量2=样本量+缓冲液量1，predilute反应量2=反应量1，twodilute反应量2=反应量1+缓冲液量2；只在对应输入全部合法时按原规则计算，不把空值/非法值转0。其余不匹配的体积保留草稿并在保存时报错，不执行原版静默置0。

旧数据兼容与数据窗口待接项：静态 ProjectAdvancedEditor::validate 允许旧动作缺少整个IBS_name键，以便旧项目仍能读取后补选；如果该键已经明确为none但动作需要缓冲液则拒绝。**请数据窗口在 ProjectsStore::validate（保存/导入入口，不是read）补查：任意IBS_amount_ul或second_IBS_amount_ul>0时，fields[IBS_name]必须存在且为有效具名缓冲稀释液。**这可同时保留旧库可读并拒绝新增缺稀释液配置。具名目录/类型/有效状态与引用保护仍由Store复核。

已做：git diff --check源码格式核查，无报错；按函数/字段逐项审查；未做本机运行、未部署或运行RK。待整合ARM64编译与RK实际用例。正式.ui没有修改，界面颜色/几何位置未重排；Linux字体沿用现有移植文件设置，原Windows截图/实屏视觉仍待核。

追加源码核查修正：样本行混匀方式沿用原changeItem()预置 none=0、slow=0.7、fast=1.3、strong=3.3 秒；只在使用者切换方式时填入，已存时间不被加载覆盖，仍可继续编辑。动作校验错误改用原界面中文名称与单位，便于定位输入。

## 固定交接版本

两份源码现已停止修改，后续仅按整合窗口发现的具体问题修正。刚才整合源码指纹检查捕捉到本窗口发布codeReady之后继续补充源码，拒绝了那轮混合输入构建；这不是功能通过。以下handoff记录为当前固定SHA，整合窗口请用当前字节重新构建，失败轮次保留。

已只读核对主页面与数据窗口当前实现：project_page.cpp已接sampleRateLabels/sampleSelections/defaultSampleLabel、validateSampleSelections和diluentOptionsReady；projects_store.cpp的writeActionReferences已拒绝使用缓冲液但稀释液缺失/none，并检查类型“缓冲稀释液”。这些是源码接入事实，仍待ARM64/RK验证。
