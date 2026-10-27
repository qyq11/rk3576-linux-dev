# 数据接口约定（2026-10-27，第一版）

正式头文件已落入 `projects/projects_store.h`；本文件只供四窗口协调，不是另一本主计划。现阶段 apiReady=true、codeReady=false，不可据此声称已构建或已在RK通过。

## 页面可直接接入的槽和回执

均在原 ProjectsStore 工作线程排队调用，沿用原权限（工程师/管理员）和 `succeeded(requestId, operation, id)` / `error(requestId, operation, message)`。成功后页面主动 load，不能在成功回执前声称已保存。

| 槽签名 | operation | 成功id |
| --- | --- | --- |
| `void create(qint64 requestId, ProjectRecord project)` | `create` | 新项目本地id |
| `void importFile(qint64 requestId, QString filePath)` | `importFile` | 最后一个新项目id；仅单位时0 |
| `void addUnit(qint64 requestId, ProjectUnit unit)` | `addUnit` | 0 |
| `void updateUnit(qint64 requestId, QString originalName, ProjectUnit unit)` | `updateUnit` | 0 |
| `void removeUnit(qint64 requestId, QString name)` | `removeUnit` | 0 |
| `void save/copy/remove` | 沿用原名 | 原约定 |

create须id=0、fixed=false，仪器项目编号由使用者明确填写。项目名称/全称1至16字符，LIS代号最多8；结果1至6行，且唯一主显示项，单位须已存在。新建完整项目建议明确提供Original/Calibration/Rate/INR四槽，禁止把Time自动改作Original。复制保留源仪器编号（原行为），得到新的本地id，不复制外部业务引用。

单位名称1至64字符，禁止控制字符、路径字符、首尾空白；倍率为有限正整数，最大2147483647，与现有正式结果显示契约一致。新增同名拒绝；修改/删除被项目结果或已登记跨库业务项目使用的单位拒绝，未引用单位可改名和倍率；不默认级联改现有结果。

## 新增数据成员

`ProjectRecord::algorithm` 类型 `ProjectAlgorithm`：`configured`、`coefficientA/B/C`、`autoApply`。未配置旧库保持configured=false；A/B/C须有限、-9999至9999、最多四位小数。原 `xishu_a/xishu_b/sample_warmup_length` 分别对应A/B/C，最后一个并非样本预热。预热的有依据字段为 `advanced.reagents[*]["reagent_warmup_length"]`（秒），沿用高级校验。autoApply对应原auto_apply，只存配置，不执行校准应用。

`ProjectResult` 增加 `selectionKnown/sourceResultId/used/display`，对应原result_id/is_used/display。只有已知项才允许写入；ID严格Original=1、Calibration=2、Rate=3、INR=4；未知旧项保持未知。三个算法启用框改对应Calibration/Rate/INR的used，同时保留display；首次明确设置时selectionKnown=true且正确填sourceResultId。隐藏MDA不开放。正式结果格式弹窗读写同一表，不能另存一份开关；`mainResult`仍是唯一主显示项，与used/display不同。

原test_analysis_method本就禁用；ldt_methon只读；d_T与collect_rate_ms实际单位未明，本批不写。校准拟合/反求/审核/应用、完整算法实现仍缺，不以参数保存代替。其余未确认校准字段不新造默认值。

## 稀释方案与高级行一致性

ProjectRecord::sampleRates由高级窗口提供最终列表；每项 `sampleRate/sampleOption/defaultOption/order`，标签非空且不重复、最多64条，order从1连续排列；非空列表必须恰好一个默认，默认必须启用。空列表允许尚未配置，不能携带高级样本行。

高级窗口保留 `ProjectAdvanced::fields/samples/reagents`；增加 `fields["IBS_name"]` 对应稀释液，未配置旧字段不注入none。具名稀释液须是有效试剂资料中类型“缓冲稀释液”的条目（原S_IBS为s_ibs；正式目录中文类型已核实）。已有字段和动作列不变。samples非空时必须与sampleRates逐行同名同序，sample_option必须等于sampleOption。默认仍以sampleRates为唯一来源，不在samples增加sample_default_option。删除/改名高级行与列表须一起提交，不静默覆盖页面请求；save不再用旧sampleRates覆盖新草稿。

## 显式JSON导入（UTF-8，sn100-project-config-v1）

文件根对象必须是 `{ "format":"sn100-project-config-v1", "units":[...], "projects":[...] }`，不读取数据库、不读取参考工程，不隐式导入演示配置。最多2MiB/256单位/1024项目/4096结果；字段错、重复名、未知单位、未登记稀释液/试剂、非有限数值、版本错、同名已存在均整批拒绝，事务回滚。units或projects可空，但不能都空。单位与项目只追加，已存在的同名单位即使倍率相同也拒绝；若复用目标已有单位，导入units中不重复列它。

单位对象字段：`name`字符串、`ratio`数值。项目字段：`instrumentProjectId/name/fullName/lisCode`字符串、`enabled`布尔、`results`数组、`sampleRates`数组、`advanced`对象或null、`algorithm`对象或null。禁止提供本地id/fixed/changedAt。results逐行字段：`type/name/lisCode/unit`字符串，`decimals`整数、`testLow/testHigh/normalLow/normalHigh`数值，`mainResult`布尔，`selectionKnown`布尔、`sourceResultId`整数、`used/display`布尔。sampleRates逐行字段：`sampleRate`字符串、`sampleOption/defaultOption`布尔、`order`整数。advanced使用现有存储的三个键 `fields/samples/reagents`，其中各字段值必须为字符串；null表示未配置。algorithm对象字段 `coefficientA/coefficientB/coefficientC`数值、`autoApply`布尔；null表示未配置。导入严格要求所列字段，拒绝未知字段以避免拼错后静默丢数据。

## 待RK核对

先用隔离空库建单位和项目，验证重复/未知单位拒绝；保存读回、取消、复制、固定/引用删除保护、单位引用拒绝；JSON部分失败后无新增行；默认方案唯一/未启用默认拒绝；算法非法数值拒绝；结果格式开关在两个编辑入口一致。由整合窗口ARM64交叉编译并在RK执行，本窗口不跑电脑测试或部署。

无需新增纯数据文件或工程接线，实施仍只改拥有的三个正式文件。

补充：新增信号 `diluentOptionsReady(QStringList diluentNames)`，与原两参数reagentOptionsReady并存；load时返回有效缓冲稀释液名称，主页面转交高级编辑器，不更改旧信号签名。


当前已发布首个可接入切片（见handoff.json实时SHA）：三个正式文件所有新增槽已有实现；高级窗口IBS_name支持已经读取核对。新增/保存/复制/删除/单位维护/文件导入均以BEGIN IMMEDIATE串行核对并提交。算法结构是新增独立表project_algorithm与project_algorithm_schema标记，旧库仅加表不回写旧行，未配置项目仍algorithm.configured=false。结果选择仅显式已知项写入原project_result_format_source，旧未知值不补默认。Store保存有缓冲量却缺IBS_name时拒绝；read仍可读取旧缺键方案以便修正。


稀释窗口补充请求已落实：ProjectsStore::validate也在保存/导入之前拒绝有缓冲量但缺少IBS_name/为none的方案；具名条目的类型、有效性及引用仍在writeActionReferences同一事务复核。read不调用这个新增保存规则，因此旧缺键方案可读，修改后须补选。


结果选择版本保护：普通load/save与格式弹窗现在一致核对current_format_schema和project_result_format_source必须成对且版本匹配；有表无标记、有标记无表或未知版本均拒绝，不自动接管或补标记。

