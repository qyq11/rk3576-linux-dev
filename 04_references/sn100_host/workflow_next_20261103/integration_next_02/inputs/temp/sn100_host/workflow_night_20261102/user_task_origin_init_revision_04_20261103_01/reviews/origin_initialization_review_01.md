# 初始化配置来源独立源码审查 01

审查人：独立子代理 `/root/origin_audit`。日期：2026-11-03。

结论：在本次定向审查范围内，未见确定的源码阻点。旧03中“样本/试剂已缓存旧配置，Session后来才读取新摘要”的问题，已在本候选的实际初始化链上关闭。此结论仅限下列冻结源码，不是编译、Windows运行、RK运行或仪器验收通过。

## 审查对象与证据

工作目录：`temp/sn100_host/workflow_night_20261102/user_task_origin_init_revision_04_20261103_01/`。下文源码位置均相对于其中的 `candidate/06_programs/sn100_host/`。

| 对象 | SHA-256及独立核对结果 |
| --- | --- |
| `before_source_manifest.json` | `7cad7a7e401f619276cfea032dcf3337c76a4522a1f52ba594a845b381b1ea7e`；实际base的685文件逐项一致，0不匹配 |
| `review_source_manifest_01.json` | `bbf468e1de179161da7ecf9a188435dde2f7d15b8ae29ca2849f33a906a2250a`；实际candidate的685文件逐项一致，0不匹配 |
| `review_source_diff_01.patch` | `6cb11db873125f81badaab385775aafe344be112b8c8cfbbf047740b52746eec` |

独立比较实际base和candidate，共20份文件不同。直接阅读实际源码，并以只读 `git diff --no-index` 比较相关文件；没有执行作者的修改、静态检查、打包或验证脚本。旧03及既有三份独审报告没有改写。当前正式工程已有未提交修改，本审查没有覆盖它们，也没有提交Git。

本次重点为新增的OriginalIni、样本仓库、试剂仓库各两份文件，以及Session、Store、GUI配置摘要消费位置；并按 `inputs/root_user_origin_independent_review.md` 复核四处CAL重叠接口。原14份User文件与旧03实际字节比较，9份相同，另5份为本次已检查的Session两份、Store实现、Store头文件和任务桥实现；没有把旧报告当作新增代码的独审依据。

## 配置字节、解析值和缓存的实际调用链

| 校验点 | 实际源码依据与判断 |
| --- | --- |
| 同次原字节生成解析值和摘要 | `original_files/original_ini.cpp:123`的`readWithFingerprint`只调用一次`readBytes`，将同一`QByteArray`传入按值接收的`parseBytes`，再对原数组计算SHA-256。去BOM和注释转换只影响解析副本；不存在第二次读文件给旧解析结果贴新摘要的路径。 |
| 来源不可随意重填 | `original_files/original_ini.h:28`起的`InitializationSource`只有私有构造和私有字段；对消费者只交`shared_ptr<const InitializationSource>`及只读getter。三文件解析值、原字节版本、逻辑根和实际根在capture中一并形成。 |
| 消费前捕获 | `samples/original_sample_store.cpp:23`先capture，再创建并初始化本轮SampleRepo、Session及内置ReagentRepo；不是让这些仓库先读INI后再capture。 |
| 样本仓库真正消费 | `samples/original_sample_repository.cpp:43`先清旧来源，核根和容器值；55行直接用source的theme建立普通/急诊孔数。79行仅在`load`成功后保存实际消费的同一指针，没有在初始化失败时保存来源证明。 |
| 内置试剂仓库真正消费 | `reagents/original_reagent_repository.cpp:147`先清旧来源；152至160行核根并用source的theme选择20或42孔的原表；163行仅在load成功后记录该指针。没有重新读取theme再沿用source版本。 |
| Session必须与样本、试剂同源 | `samples/original_detection_session.cpp:242`先清版本和来源；246行缺省参数仅能复用样本仓库实际记录的指针；247行拒绝外传另一指针或已过期来源；254行显式向内置Reagent传同一指针，258行再核它确实消费了同指针。260行读取冻结的三份解析值。 |
| 成功前核当前性 | Session在277行重核后才保存来源/版本；Store在39行再次核当前文件，随后才有41行首个move、69行ready及72行以后就绪信号。失败时放弃局部对象，不发布这轮ready；这不代表项目仓库原恢复动作被回滚。 |
| 根和当前文件 | `original_files/original_ini.cpp:167`的`matchesRoot`同时核绝对逻辑路径、canonical实际路径及目录存在；176行的`current`在读三份当前原字节SHA前后均核根。capture本身也在163行做一次current。原read的逐级符号链接拒绝仍在13至37行的共用读取函数中。 |

三份INI不是一次原子读取；这里的关键是每个文件的解析值与其摘要来自同一次读取，所有新缓存消费同一个冻结对象，并在安装前重新核文件。短暂发生A→B→A，也不会再将B解析值与另次读取的A摘要拼成来源。该对象的证明范围明确限于本轮样本、内置试剂及任务会话，不替main或其他页面的全部早期缓存背书。

## 原读取语义和main参数

- 对比base的OriginalIni实现，原`read`的UTF-8有效性/NUL检查、BOM处理、真正整行`#`注释兼容、引号及续行处理、私有临时文件及仅所有者读写权限、QSettings的UTF-8和QVariant取值、大小写查键规则均保留。`read`仍在入口清输出和错误，失败不返回不完整解析结果。合法空INI没有被新增的来源捕获拒绝，原始空字节也有有效SHA。
- `original_files/original_ini.cpp:189`的容器布尔规则与未改的`original_runtime_settings.cpp:29`及44行一致：准确键优先，否则唯一大小写匹配；true/1、false/0/空串及缺省false按原规则解释，不将false当作“尚未配置”。SampleRepo在50行核显式传入值与冻结值相等后才新建急诊空孔。`shell/main.cpp:586`仍向Store传实际runtime值。
- `samples/original_sample_store.cpp:29`只在已有显式layout时核普通/急诊孔数，不把内部缺省孔数冒充先前main配置。显式layout不一致会拒绝这轮初始化，不给旧layout采用新来源。
- `shell/task_reagent_shell.cpp:668`登记、630行当前性核对均使用`OriginalIni::fingerprint`；Session保存的是source的同一组三份原INI SHA。配置摘要不再混用带数据库/WAL语义的`databaseFingerprint`。项目数据库版本仍用原项目版本算法，两类证据没有互换。

## 用户准备入口和CAL合并保护

`samples/original_detection_session.cpp:468`的User入口在prepare前后都核：来源类别、允许动作、root、三消费者同一来源指针、初始化三份版本、当前原INI、已保存样本任务摘要和项目版本。未带source的旧接口仍可走原Device兼容初始化，但`configurationSource_`为空，不能通过User入口；没有晚捕获一份新来源替旧缓存续签。立即下发为true仍明确拒绝，未改走Ts。

实际User消费仍为`Action::None`：GUI登记和冻结 → `SampleStore::prepareUserDeviceTasks` → `Session::prepareUserOperation` → 原prepare返回 → 真实`NoCommit`完成 → 桥退休 → 原开始准备收尾。`samples/original_sample_store.cpp:314`线程先守卫仍在；340行才finish并发真实完成。`shell/task_reagent_shell.cpp:706`仍先核完整完成归属，不匹配不会标processed。没有将本次初始化修复扩称为User真实reserve、SQL写入、页面写入采用或Emergency已实际接通。

四处CAL重叠接口未见原保护丢失：

1. `samples/sample_store.h:32`及未改的`main.cpp:592`仍将真实试剂资料持有的metadata共享给Store；Store在`original_sample_store.cpp:32`继续传给Session，Session在251至252行绑定同一拥有者到内置Reagent。
2. Store的`original_sample_store.cpp:409`先核真实Tt/TR来源、arrivalPacketId、connection及真实payload摘要；Session的`original_detection_session.cpp:635`再拒绝User及不符Device来源。上述校验都在真实Tt的CAL两把锁之前。
3. Session真实Tt仍按667行取项目/试剂两锁 → load/捕获 → `tasksForRack`编排及局部setter事实 → 674行publishDefaults。随后reserve在518行重新取原两锁，520至522行核原版本、metadata当前性及扣量工作，再进入原事务；没有拿User来源替代CAL批号票据或试剂量来源。
4. `prepare`的443行只capture到局部work。User入口调用这条预检查路径，不调用publishDefaults或reserve；真实Tt返回后Store的420行仍发metadata变化通知，包括setter已到达而后续失败的情况，没有改成只在成功/COMMIT后才通知。

## 未验证和交接边界

- 本报告未编译、未运行产品或测试、未联网、未联板、未占共享显示、未读写参考工程配置；没有Windows实际运行证据。Windows原版操作及行为是否遗漏仍由总窗口按可用条件核对，不得把本报告称作Windows运行通过。
- 待总窗口在批准的构建和RK/真实仪器条件下核：正常三配置初始化与开始；初始化期间配置改变后的拒绝/重载；缺省false及显式true容器一致性；显式孔数不符后的拒绝；同一任务开始的真实NoCommit退休；原Tt CAL两锁、setter变化通知、后续失败及reserve复核。
- 本次未扩大审查为全工程所有配置消费者同源，也未验证真实到包、账户切换、断线、乱序及CAL竞争的运行时序；已有来源、账户、资料、连接和事务事实仍分别保留，不可相互替代。

本次只新增本独审报告；无候选、正式源码或公共计划/记录修改。
