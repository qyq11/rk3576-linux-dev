# User初始化修订04：配置同源与CAL接线独立窄审

2026-11-03。本次只读送审目录 `temp/sn100_host/workflow_night_20261102/user_task_origin_init_revision_04_20261103_01/` 的实际candidate、相关差异和必要直接调用。开审时没有README或单独送审md，先以总控“送审非最终片”指令及 `stage_result.json` 界定输入；审尾新出现 `handoff.md`，已完整回读。未把作者两份审查当结论。只新增本报告，不改正式/候选，不复制或回演685树，不构建、运行产品、执行SQL、模拟、联网或联板，未开子Agent。原03材料和修订原件保持。

**在本次所审的初始化配置同源、失败拒绝及四处CAL接线范围内，未见确定的新源码阻点。** 这是指定送审快照的静态窄结论，不是最终冻结、正式晋级、整个用户来源链通过、编译或RK通过。主入口其他运行配置缓存、独立试剂页面拥有者、Emergency立即下发和所有运行中配置切换不由这个初始化对象一并证明。

## 本次实际输入

- `review_source_manifest_01.json` SHA-256：`bbf468e1de179161da7ecf9a188435dde2f7d15b8ae29ca2849f33a906a2250a`。
- `review_source_diff_01.patch` SHA-256：`6cb11db873125f81badaab385775aafe344be112b8c8cfbbf047740b52746eec`。
- `before_source_manifest.json` SHA-256：`7cad7a7e401f619276cfea032dcf3337c76a4522a1f52ba594a845b381b1ea7e`。只在内存比较前后清单：685→685，20修改、0新增、0删除；20项实际candidate SHA与送审变更表及送审清单逐项相同。没有据此声称现场正式整树仍等于某个旧684前像。

下文路径相对该目录的 `candidate/06_programs/sn100_host/`。检查重点是INI来源新增的8个文件/接口及CAL共享4文件的直接消费者；其他继承的用户来源桥、采用/读回/关闭等20项中的差异不在本报告中整批放行。

## 同次原字节到不可变配置来源

`original_files/original_ini.h:23–43` 用 `shared_ptr<const InitializationSource>` 传递，构造私有，外部只有const解析值、版本和核对接口；没有可重新抓版本并替旧缓存续签的setter。source同时保存所选绝对根与实际canonical根。

`original_ini.cpp:13–37` 一次读取原文件bytes并核路径、错误及本地大小预算。`:40–110` 的parseBytes按值接收副本，去BOM及整行注释适配只改私有副本；`:123–132` 解析成功后，对仍保留的同次原bytes求SHA。解析值和摘要不是两次独立打开文件，摘要也不是兼容临时文件的SHA。UTF-8、NUL或解析失败不返回可采用的digest。

`capture:144–164` 对 `config.ini`、`config_Debug.ini`、`theme_config.ini` 分别形成解析值和原字节摘要，再调用current。`:167–186` 复核所选根、实际根及三文件当前原字节，不把后来的文件版本写回已有对象。这三文件不是原子读取；代码采用捕获后、安装前和用户准备前后的重复拒绝检查，未声称有文件系统原子快照。

## 首个消费者前捕获，三个消费者真正用同一对象

`samples/original_sample_store.cpp:19–40` 先release旧对象、核工作根，再capture；capture位于创建/初始化SampleRepository之前。后续仓库、会话、同步对象先保留在局部unique_ptr，任何失败直接返回；最终current通过才在`:41–43`安装对象，`:69–76`发ready与配置/项目值。没有“先用磁盘读旧配置，再捕获新摘要给旧缓存背书”的顺序。

三个消费者的实际读取如下：

| 消费者 | 实际使用 | 同源/失败核对 |
| --- | --- | --- |
| `OriginalSampleRepository::initialize:40–79` | source中的新孔容器值和theme普通/急诊孔数，用于真正创建Emergency孔；source存在时不再另读theme.ini | matchesRoot；容器参数须与source解析一致；初始化/load成功后才记录这份source |
| `OriginalReagentRepository::initialize:144–164` | source中的theme选择20/42原表；不是另外打开theme.ini选表 | matchesRoot；load成功后才记录source；失败先清配置来源指针，不发布虚假已消费标记 |
| `OriginalDetectionSession::initialize:238–280` | normal/debug/theme都从source取得，初始化扫描、TaskSendDelete、行次序、报表及多瓶例外缓存 | source必须与SampleRepository同指针；将它传给内置ReagentRepository并核同指针；初始化前后current，通过才保存版本和source |

这里的ReagentRepository是Session内置仓库。新头注释也明确，这个对象不替main或其他页面的配置来源作证明；没有把独立ReagentStore/页面的全部初始化改成同一对象。

## main提前设置的值与失败路径

`shell/main.cpp:585–601` 在moveToThread之前设置样本普通/急诊孔数、新孔默认容器及同一批号拥有者，默认容器设置失败直接退出；页面侧`:1056–1062`也按原值设置。新对象未改写main的原解析过程，而是在实际消费者接续时核一致性：

- `InitializationSource::defaultContainerIsTube:189–200` 与 `OriginalRuntime::IniReader::flag:29–51` 的准确键优先、唯一大小写匹配、布尔文字/缺省规则一致；不是另发明一套杯/管默认值。
- SampleRepository `:47–51` 把主入口提前传来的容器bool与本次source值比较，不同就拒绝，不给旧bool贴新source。
- Store `:29–30` 在layoutConfigured时核普通/急诊孔数与仓库真实消费结果一致；不同即失败，不把旧页面孔数当作当前source布局。
- Store `:39–43`在安装局部对象前current；Session `:277–279`在记录已消费来源前current。初始化读/解析/配置不一致失败时没有ready或开始发送；原项目仓库初始化可能有其既有恢复动作，错误路径并未伪称这些动作已回滚。

本项证明的是这些提前参数与新样本消费者的值一致及三消费者同源，不证明main所有RuntimeSettings、独立主题界面或独立试剂页全部消费同一原字节。运行中更改配置仍需要按现有来源撤销/重新加载和对应实机条件核对。

## 旧接口、用户准备与指纹种类

Session旧初始化若未显式传source，只能借用 `samples->configurationSource()`（`:245–246`），不能初始化后另capture一个新对象。旧SampleRepository没有source时仍走原磁盘读取，但Session最终没有可用source。`prepareUserOperation:473–478`要求source存在、三个消费者同指针、初始化版本恰为三项且等于用户请求版本，并调用current；因此无source的legacy缓存不能作为UserOperation来源，即使调用者另外提供了当前三文件SHA也不成立。

`prepareUserOperation:468–496`先撤未发送准备，不清已发任务或退量依据；在prepare前后两次核配置、已保存样本输入摘要和项目版本。SendTaskImmediate从同一source的debug读取，true时明确拒绝未接的Emergency流程，不误改成普通Ts。prepare读取/计算失败保留false，二次来源失败会cancelUnsent，不把前一次prepared留作新请求成功。

User来源的三INI版本在 `shell/task_reagent_shell.cpp:667–668`由 `OriginalIni::fingerprint`取原字节SHA；`:629–631`按相同方法复核。项目文件在`:639–642`用 `OriginalProjectRepository::databaseFingerprint`；该方法底层 `projects/original_project_repository.cpp:64–81,793–795`核数据库主文件和存在的 `-wal`，包含既有suffix标记。配置字节与数据库/WAL不是同一种摘要，不再借数据库摘要函数给INI签版本，也不忽略项目WAL。

用户动作完成后的主窗口仍在 `task_reagent_shell.cpp:501–507`核未取消、epoch及userTaskOriginCurrent；失败排cancel而不开始。`device_control_shell.cpp:187–202`还核原请求及当前设备/业务阻断，success为false不发Ts。这里不重审整套用户桥的异步取消/关闭合同，只确认配置检查失败没有被本条调用改写成开始成功。

## 四处CAL共享接线没有在文字合并中丢失

1. **同一拥有者。** `samples/sample_store.h:31–35`保留move前绑定shared模型；`main.cpp:592–601`仍传真实ReagentStore模型。Store初始化`:32`把 `originalPageMetadata_` 与configurationSource一同交Session；Session `:250–254`保留同一pageMetadata并传给内置Repository，初始化新增清除的是configurationSource，不是批号模型。
2. **预检查不发布。** Session `prepare:427–465`捕获私有work，`prepareUserOperation`复用此prepare；没有publishDefaults、reserve或SQL写入。取消未发准备只改prepared，不回滚原盘面Default。
3. **真实Tt发布与双锁。** `answerRackRequest:663–679`仍以项目→试剂两锁覆盖load/capture/编排/publish，先发布已到达setter的Default事实，再返回allocated失败；随后释放外锁，reserve `:510–522`同序重新取锁，核同一work的当前模型修订和原版本。新增来源校验仅在入口先拒绝User伪Tt，没有把真实Tt改成用户准备或另造拥有者。
4. **失败也通知盘面。** Store直接Tt路径`:367–369`以及带真实包来源的路径`:417–424`都保留Session返回后发 `originalReagentPageMetadataChanged`，失败不因缺成功报文就漏掉已经发生的setter；真实提交外层失败仍保留原任务/结果事实通知。

本项只确认这四处接线与新增初始化来源能同时成立，不重判CAL完整原版等价，也不关闭42/48孔对应、旧非任务提交未知、真实Ur晚到、立即Emergency编排或全部N/C并发问题。

## 送审状态及下一步

本报告绑定上述manifest/patch和对应20项实际源，不将作者“送审非最终”改称冻结完成。最终交付如更换任一受审字节，应给新清单/补丁并标出差异，由总控决定需要的补审；不能把本次静态结论直接贴到以后版本。作者须在最终交接明确冻结状态、审查归属及实际未验范围。本片必要完整ARM构建、RK真实初始化/重新加载/配置变化拒绝、实际开始及失败恢复仍待总控安排，不以本报告替代。

审尾再核：指定送审manifest/patch仍为本文所列SHA，20项实际candidate均未偏离送审后像。新handoff仍说作者侧两审在进行，未据此升级结论。其第72行一处包装对应关系须在最终交接前更正：送审 `review_source_diff_01.patch` 实际仍为 `6cb11db8…2746eec`，`packaging_01/user_task_origin_shared_initialization_on7cad.patch` 实际为 `269d0ad8f20a3c8978b69a12865ced365a6e9bdbb23586004fa51fc87ba2f781`，两者不能写成“字节相同、SHA均269d”。这不是本次发现的源码业务阻点；应明确普通送审差异和最终Git补丁各自用途及指纹，保留原件，不为统一SHA覆盖原送审文件。作者已补handoff入口，最终冻结状态、文档精确对应关系及实际未验范围仍须收齐。
