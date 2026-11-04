用户任务准备的初始化来源与结束顺序独立复查，2026-11-03。

结论：未见本次所审源码的阻点。候选先固定三份INI的解析值和原始字节摘要，让样本仓库、任务会话及其内部试剂仓库消费同一只读对象；全部消费者完成后，再核对当前文件，才安装对象并发布就绪。取消、断线、错误来源和重复完成回执仍受原结束顺序保护。本结论只来自实际源码和前像的静态比对，不是编译、Windows运行、RK运行或真实仪器验证通过。

本报告独立读取候选及7cad正式前像，未以作者脚本、静态结果文件或其他复查者的结论代替审查。只新增本报告，未改源码、旧报告或公共文档，未构建、运行产品或测试、模拟设备、联网、联板或占用共享显示。

送审范围已逐文件核对：前像与候选均为685个文件，文件名集合相同，没有候选清单外的源码文件，实际变化20个文件；两侧文件摘要均与各自冻结清单相符。绑定的清单及差异摘要如下。

| 文件 | SHA-256 |
| --- | --- |
| `before_source_manifest.json`（7cad前像） | `7cad7a7e401f619276cfea032dcf3337c76a4522a1f52ba594a845b381b1ea7e` |
| `review_source_manifest_01.json`（本次候选） | `bbf468e1de179161da7ecf9a188435dde2f7d15b8ae29ca2849f33a906a2250a` |
| `review_source_diff_01.patch` | `6cb11db873125f81badaab385775aafe344be112b8c8cfbbf047740b52746eec` |

初始化的实际顺序和失败边界如下。

| 核查点 | 实际源码依据与判断 |
| --- | --- |
| 先固定来源，再读取业务配置 | [original_sample_store.cpp:23](C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/workflow_night_20261102/user_task_origin_init_revision_04_20261103_01/candidate/06_programs/sn100_host/samples/original_sample_store.cpp:23)在新建样本仓库、任务会话前调用`capture`；第27、32行将同一指针传给消费者。设备同步对象的初始化仅检查工作根目录，不额外读取这三份INI。 |
| 解析值和摘要来自同次实际读取 | [original_ini.cpp:123](C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/workflow_night_20261102/user_task_origin_init_revision_04_20261103_01/candidate/06_programs/sn100_host/original_files/original_ini.cpp:123)用同一`QByteArray`解析并计算摘要；解析函数按值接收，去BOM及兼容注释不会改掉用于摘要的原字节。`capture`固定实际根目录、三份解析结果和摘要，捕获后再核当前文件；对外只提供`shared_ptr<const InitializationSource>`。 |
| 三个消费者确实用同一对象 | [original_sample_repository.cpp:40](C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/workflow_night_20261102/user_task_origin_init_revision_04_20261103_01/candidate/06_programs/sn100_host/samples/original_sample_repository.cpp:40)从来源对象读取主题配置，成功后才记录指针。[original_detection_session.cpp:238](C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/workflow_night_20261102/user_task_origin_init_revision_04_20261103_01/candidate/06_programs/sn100_host/samples/original_detection_session.cpp:238)要求与样本仓库指针相同，将同一指针交内部试剂仓库并核对其消费结果；会话的普通、调试、主题配置均从该对象取值。 |
| 最后核对在安装和发布之前 | [original_sample_store.cpp:39](C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/workflow_night_20261102/user_task_origin_init_revision_04_20261103_01/candidate/06_programs/sn100_host/samples/original_sample_store.cpp:39)的`current`位于首次`std::move`之前；安装在第41至43行，`ready_=true`在第69行，Profile、Units、Projects及ready信号在第72至75行。期间没有显式等待或嵌套事件循环。 |
| 失败没有发布半套新对象 | 通过线程、质控占用守卫并执行`release`后，本次新初始化的失败分支均返回；局部对象按设备同步、会话、样本仓库的逆序销毁，会话持有的仓库指针不会因局部析构顺序先失效。[sample_store.cpp:951](C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/workflow_night_20261102/user_task_origin_init_revision_04_20261103_01/candidate/06_programs/sn100_host/samples/sample_store.cpp:951)的原`release`把`ready_`置false；后续失败不会保留旧仓库继续作为就绪对象。入口守卫在`release`之前拒绝时则保留原状态。 |
| 外部默认容器及显式孔布局必须一致 | 样本仓库将传入默认容器与冻结的`userOption/isContainerTube`比较，冲突就拒绝；布尔读取与主程序现用`IniReader::flag`的准确键优先、唯一大小写匹配及合法值规则一致。Store旧调用未显式设置时仍是原杯默认false；冻结值为true会拒绝，不能静默换容器。Store第29行只对已显式配置的普通/急诊孔数比较，不把外部旧缓存认作本次新来源。 |

失败时“放弃局部对象”不等于“所有文件改动已回滚”。[original_project_repository.cpp:658](C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/workflow_night_20261102/user_task_origin_init_revision_04_20261103_01/candidate/06_programs/sn100_host/projects/original_project_repository.cpp:658)保留原`recoverPending`，可能在最终来源检查前恢复项目复制事务；后续检查失败只会不安装新对象，不撤销既有恢复事实。本次没有新增恢复写入、扩大参考目录写权限或声称回滚此副作用。

旧接口与用户入口的边界成立。仓库省略新增Source参数时保留原主题读取和传入默认容器行为；会话省略Source只可复用样本仓库实际消费过的同一对象，不能另捕获较新文件给旧缓存补证明。真正无共享来源的旧设备接口仍可走原业务，但[original_detection_session.cpp:468](C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/workflow_night_20261102/user_task_origin_init_revision_04_20261103_01/candidate/06_programs/sn100_host/samples/original_detection_session.cpp:468)的用户准备要求三个指针相同、初始化版本完整、请求版本一致及当前文件仍相同，因此无来源旧初始化不能冒充用户来源已核。

用户准备还在原`prepare`前后核对已保存样本输入摘要及项目版本；任务剩余秒数不属于输入身份，QC/CAL身份属于输入身份。立即下发配置为true时显式拒绝，未接Emergency或改走其他命令。当前实际用户消费者仍仅为普通开始前的无写入准备：没有用户来源的选瓶扣量事务，没有用户来源TR退量，也没有草稿保存业务。不能把这一片称为用户写入、页面采用及设备执行的整链完成。

结束顺序按实际源码继续保持。

- [original_sample_store.cpp:314](C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/workflow_night_20261102/user_task_origin_init_revision_04_20261103_01/candidate/06_programs/sn100_host/samples/original_sample_store.cpp:314)先核线程，错误线程不读仓库或账户，也不伪发后台完成。实际排队工作只有Session返回后才`writer.finish`并发一次真实完成回执。
- [task_reagent_shell.cpp:706](C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/workflow_night_20261102/user_task_origin_init_revision_04_20261103_01/candidate/06_programs/sn100_host/shell/task_reagent_shell.cpp:706)先核来源、完整basis、动作号、实际包号、连接、根目录及摘要，再置processed；错误来源只保留待核事实，不能释放当前工作。重复完成不再次启动；无写入形状检查同时要求事务未开始、无SQL、无任务和版本/盘面变化等。
- 第694、766、780行的取消、身份撤销与关闭仍遍历内部actionId队列。未派发动作可取消退休；已派发动作保留到真实完成，关闭等待事务和专用读回结束。超时只停止本次开始等待，不伪造回滚或重发；断线还会改变请求号，使旧完成不能开启新连接。
- 第420行的设备processed仍按真实设备packet反查，并核连接及Tt/TR类别；用户packet为0，不被当作设备包。第444、467行分别核页面采用回执和实际读回完成，不能用另一回执清除读回等待；页面读回仍在工作处理返回后安排。
- 第476行的共同收尾先等处理、读回和页面回执，再退出actions；之后才复核用户当前连接、账户、配置、项目和样本输入，并调用原开始回调。取消、错误来源、未知事务或复核失败会以false结束；[device_control_shell.cpp:187](C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/workflow_night_20261102/user_task_origin_init_revision_04_20261103_01/candidate/06_programs/sn100_host/shell/device_control_shell.cpp:187)只有当前请求仍有效且success、现有阻断检查全部通过才调用`start`发送原开始命令。

对旧03实际字节的补核：收据结构、收据写入器、设备控制入口、运行量处理、任务显示和试剂页任务文件均与旧03相同；桥接文件仅改三INI的摘要读取函数并加入头文件。因此旧03的完整来源归属及非法User清除信任摘要、保留真实事务事实的限制仍在。相对本次7cad前像，试剂仓库实现只改初始化；Session原`prepare(bool)`、`reserve`、`tasksForRack`、`capturePageMetadata`及Store的`prepareOriginalReagentUpdate`正文没有改动。真实Tt/TR仍走原Session/reserve账；Us及CAL锁批号/回执正文未被本片替换。本结论不等于对这些旧业务重新完成实机验收。

来源证明的范围是Store这一条链中的三份INI及三个配置消费者。主程序、其他页面更早缓存的连接、机型、运行选项、布局或其他资料，没有因共享对象而自动成为同来源；本片只对进入Store的容器和显式孔布局核值相同，未证明整个应用使用同次文件字节。三文件和最后核对也不是文件系统原子快照，不能表述成后续文件永远不会改变；实际用户准备入口、准备后和退休前的再核仍须保留。

本轮没有Windows实际运行核对，也没有Windows重构建、RK或真实仪器结果。后续整合应保留初始化失败无就绪发布、取消/断线/关闭后晚到完成不发Ts、重复回执不重复采用、读回完成前不退出以及普通Tt/TR、Us、CAL原流程的实际联机验收项。真实用户写入、Emergency或其他新消费者需要另行建立其动作、提交与恢复证明，不能直接沿用本次NoCommit结论。
