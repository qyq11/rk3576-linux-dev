# 普通开始请求与初始化配置来源：独立候选04

本片是**已冻结、待总控串行整合的源码候选**。它把原03的用户无写准备链重基到已批准的ASCII和校准盘面批号源码，并修复“旧配置缓存被贴上新配置摘要”的初始化缺口。立即下发、真正的用户扣量事务、Emergency补发没有在本片实现。没有修改正式源码、公共计划/记录/README、参考工程或其他候选；没有编译、运行产品/测试、联网、操作RK/仪器/共享显示、提交或清理。

## 唯一接续前像与应用范围

- 前像为总控批准的7cad组合源码，685份：`before_source_manifest.json`，SHA-256 `7cad7a7e401f619276cfea032dcf3337c76a4522a1f52ba594a845b381b1ea7e`。来源为总控`integration_10_source_acceptance_01/cal/line_ending_correction_01/source_after.json`，不是CAL首次换行不符的失败表。
- 本目录`base/`保存这685份精确前像，`candidate/`保存685份候选。20修改、0新增、0删除；没有新编译单元，不需要修改公共main、构建脚本或工程清单。
- **累计补丁只应用一次**：`packaging_01/user_task_origin_shared_initialization_on7cad.patch`，SHA-256 `269d0ad8f20a3c8978b69a12865ced365a6e9bdbb23586004fa51fc87ba2f781`。它已经包含03的14份用户来源改动，不能先应用旧03再应用本片，也不能拿03整文件覆盖CAL。
- 后表：`packaging_01/after_source_manifest.json`，SHA-256 `b7e283018ed0ab01457effcb8f93920f454bb326f3e19838b88b72e8427512c9`。送审表`review_source_manifest_01.json`的SHA为`bbf468e1de179161da7ecf9a188435dde2f7d15b8ae29ca2849f33a906a2250a`，两表只是JSON缩进不同，路径和全部685份源码摘要相同。
- 旧03目录、补丁、报告和清单均保留。核对时旧清单SHA仍为`59cb193181ffba5cd3a334acc5ae3770eca3e0ffa383c452ada457ccc4f31855`，其中91份已登记原件逐项匹配。没有新建747前像草稿；本04直接从获批的7cad建立。
- `stage_result.json`记录**四个**CAL/User文件执行`git merge-file`均真实退出0：Session头/源、SampleStore头/原库源。其余10份原User文件的前像未变，逐SHA核后继承。文本合并退出0不单独证明业务无遗漏。

## 改了什么，使用者会遇到什么变化

开始检测前，需要确认用于准备任务的孔数、试剂表和任务选项来自同一次加载。原03先初始化样本仓库、再初始化Session内的试剂仓库，最后才抓三INI摘要；配置在前两步后改变，会使旧缓存看起来属于新版。

现在`SampleStore::initializeOriginal`在任何新仓库读取配置前，先确认原资料根并捕获三INI的同一次读取结果。样本仓库、Session和内置试剂仓库都消费这一只读对象。全部初始化完成后，在安装第一个新对象和发布任何就绪信号之前，再核实际文件摘要和资料根。改变或不一致时只报告初始化错误，不发布新资料已就绪；操作者应重新加载资料，不能继续沿用那次混版开始请求。

正常配置不改变线协议、命令、字段、编码、精度、单位、SQL扣退量或权限。主程序提前传入的新孔容器默认值必须与同一配置相符，显式布局的普通/急诊孔数也要相符。未显式配置容器时，原默认仍是杯；如果这个默认与本次配置的试管值矛盾，本Store初始化会拒绝，不静默改孔内的容器或给旧默认贴新版摘要。旧仓库调用省略新增来源参数时仍走原直接读取和杯默认行为，但这种路径不能通过新的用户准备来源校验。

## 可追踪的配置调用链

| 位置（均指candidate下源码） | 实际校验、消费与失败结果 |
| --- | --- |
| `original_files/original_ini.cpp:123`，`readWithFingerprint` | 从同一次原文件读取生成解析值和SHA。SHA针对未去BOM、未替换注释的原字节；解析只改私有副本。旧UTF-8/NUL校验、BOM处理、整行注释、引号/续行、QVariant及空INI默认语义保留。原文件不改写，原字节不写入日志、清单或交接。 |
| 同文件`:144`，`InitializationSource::capture` | 在同一clean/canonical资料根捕获`config.ini`、`config_Debug.ini`、`theme_config.ini`，对象只读，私有保存解析值和摘要。三文件不是原子快照，捕获末尾先复核，安装前再复核；不宣称消除了文件系统的全部并发窗口。 |
| `samples/original_sample_store.cpp:23` | capture早于SampleRepo/Session/内Reagent初始化；失败在此结束。`OriginalDeviceSync::initialize`本身只核资料根，不读取这三INI，无需替它贴配置来源。 |
| `samples/original_sample_repository.cpp:40` | 同Source核根和容器，再从Source的theme读取孔数。仅成功load后记录真正消费的Source指针，不在读完后补抓来源。 |
| `samples/original_detection_session.cpp:238` | 显式Source必须等于SampleRepo真正消费的对象；省略参数也只可复用仓库已有Source，绝不晚捕获新版。保留第三参数的同一CAL metadata拥有者，新增第四可选配置Source参数。 |
| `reagents/original_reagent_repository.cpp:144` | 内置仓库显式消费同Source的theme以选择20/42表；load成功后记录Source。其余试剂选瓶、优先级、扣量实现不改。 |
| `samples/original_sample_store.cpp:39` | synchronization初始化完成后、首个`std::move`安装之前最终`current`。失败只销毁局部sync→Session→Repo，不发Profile/Units/Projects/ready。原`release()`已使旧Store不就绪，不能称失败保留了旧就绪仓库。 |
| `OriginalDetectionSession::prepareUserOperation:468` | 要求三消费者指针相同、已核三INI初始版本与用户请求匹配，并核当前文件/样本/项目。无Source的旧路径拒绝User；准备期间不捕获新版本替旧请求续签。立即模式判断直接取已冻结debug，仍明确拒绝未实现的Emergency业务，不能回退发送Ts。 |
| `shell/task_reagent_shell.cpp` | 用户登记、派发/退休复核均改用相同INI原字节SHA。项目DB仍保留原含WAL的数据库指纹，两者不混用。 |

这份证明只覆盖本Store链的三个初始化消费者，以及传入的容器/显式孔数一致性。main的连接地址、配置机型、其他RuntimeSettings、ReagentStore和页面自己的早缓存不因本Store发布ready而获得“同次读取”证明；本片没有重写公共main整个加载图。

失败不发布新缓存，不代表全部原资料副作用被回滚：原`OriginalProjectRepository::initialize`仍可能执行已有`OriginalProjectCopy::recoverPending`。本片没有删除、重写或承诺回滚该项目恢复动作。

## 原03的开始与真实设备包链

普通开始：原`requestDeviceStart`通过确认及守卫后，登记独立UserOperation/actionId、真实packet=0、设备payload摘要为空，绑定原request/账户/权限/连接/资料/三INI/项目/已保存样本任务输入；排队到真正所属Worker调用Session原`prepare`。该入口不reserve、不占报告号、不扣量、不发送任务。实际finished归属完全匹配且确为None/NotStarted/无写事实后，先退休动作，再经过原request守卫调用旧`finishDeviceTaskPreparation`，才可能排队原Ts。队列失败、配置/身份变化、取消、超时或来源冲突都不能被当作设备执行成功。

保留03的设备包和用户操作分路、真实finished归属、重复/冲突事实、断线/关闭等待以及队列预算。User的None准备真实有生产调用者；User→reserve→SQL提交→页面采用依然没有生产业务入口。扩展writer/Provider/页面签名只是来源识别接口，不是SQL准入或事务回滚机制。没有伪造Tt/TR，没有自动重发不确定任务。

本片与CAL共有六个文件：原四处Session/Store，加本次修改的ReagentRepo头/源。真实Tt仍先核Device来源和原payload，再取项目→试剂两锁，load/capture/编排后publish默认批号，reserve复核同一revision。失败后metadata通知仍保留。普通prepare只capture局部批号，不publish。`prepare(bool)`、`reserve`、`tasksForRack`、`capturePageMetadata`和Us的`prepareOriginalReagentUpdate`正文与7cad原文一致；CAL其余11份和ASCII两份全字节不变，公共main/build不动。静态文本比较证据是`static_scope_result.json`，它不是运行结果。

## 修改文件

| 用途 | 文件（相对06_programs/sn100_host） |
| --- | --- |
| 同一次读取的解析值/摘要、只读初始化来源 | `original_files/original_ini.h`、`original_files/original_ini.cpp` |
| 真正消费来源并记录同一对象 | `samples/original_sample_repository.h`、`samples/original_sample_repository.cpp`、`reagents/original_reagent_repository.h`、`reagents/original_reagent_repository.cpp` |
| Store最早捕获/最后核对，Session初始化与User准备 | `samples/original_sample_store.cpp`、`samples/sample_store.h`、`samples/original_detection_session.cpp`、`samples/original_detection_session.h` |
| 来源、回执和动作匹配（继承03） | `samples/original_task_reagent_receipt.h`、`samples/original_task_reagent_receipt_writer.cpp`、`shell/task_reagent_context.h`、`shell/task_reagent_shell.cpp` |
| 普通开始和页面/Provider采用（继承03） | `shell/frmmain.h`、`shell/device_control_shell.cpp`、`reagents/original_reagent_level_task.cpp`、`reagents/original_task_reagent_display.h`、`reagents/original_task_reagent_display.cpp`、`reagents/reagents_page_task.cpp` |

精确20项前后SHA在`packaging_01/changed_files.json`。新增API均位于既有编译单元：`OriginalIni::readWithFingerprint/fingerprint/InitializationSource`，SampleRepo/ReagentRepo追加可选Source，Session保留CAL metadata参数并追加可选Source。总控须成套整合20份，不能只摘开始按钮入口。

## 原Windows对照证据和已知差异

本次**只读源码核对，没有Windows实际运行核对**，没有重新构建原程序或用原配置连接设备。已有Windows运行条件及最终实际操作核对由总控安排，不把这里的阅读或Git回演称作Windows通过。

原`Seerain450_PC/main.cpp:65/137/141/143/168`分别读取立即下发、试剂盘孔数、普通/急诊孔数和默认容器键；键、默认及已有Linux值解析规则保留。原`frmmain/frmmain.cpp:1453`的`on_btnStart_clicked`，其中`:1507`后立即模式会检查保存草稿、急诊盖、水/杯组合，再发内部Emergency信号并return，不发送Ts；原`frmsample/frmsamplematerial.cpp:3810`的`slotSendRackTasks`可对Emergency处于RackReady/RackTesting的任务编排。本片没有把这一未移植业务冒称完成，仍保留显式拒绝。

本次初始化Source是Linux为防止异步准备混版而增加的来源检查，不宣称Windows原版有同名机制。原TR恢复、Tr结果、Tt编排、CAL setter与Us保存回读规则沿用已审前像，没有放宽回包字段校验，也没有另造扣量逻辑。

## 实际做过的核对

`packaging_01/git_result.json`记录独立replay下普通Git：init、check-attr、apply --check、apply，四步实际退出`0/0/0/0`。没有使用`--ignore-space-change`、`--reject`或事后覆盖回演文件。

`packaging_01/verification_result.json`，SHA-256 `3d71bb0d9defffe0338c68f7eb0c158240cb651b243b3963bd437ba8fc3814ab`：PowerShell入口实际退出0；base、candidate和实际Git回演各685份匹配各自源表，源表无增删；核对当时正式685仍全部等7cad；旧03清单和91份原件未变。送审`review_source_diff_01.patch`的SHA为`6cb11db873125f81badaab385775aafe344be112b8c8cfbbf047740b52746eec`，与最终补丁只在receipt头/源的文件块先后顺序不同；二者涉及同20项、对应同一前后源码，原件都保留，不能混写两个补丁文件的SHA。

作者侧两份定向独审已完成，均实际读源码、独立核685前后表和20项差集，未见各自范围的确定源码阻点。送审后20项源码从未改变，报告归属保持有效，不把静态审查称为运行通过：

- `reviews/origin_initialization_review_01.md`，SHA-256 `0b2718050345a29d3cdb94e513296ff7fe883565861f644c9aed332d1b3d04a2`，确认旧03晚抓配置摘要缺口在三消费者源码链关闭、CAL共享与真实Tt顺序保留。
- `reviews/lifecycle_initialization_review_01.md`，SHA-256 `ed9797ec3ed6230bc52a8073b5a9ad43c9fe27ada91ff5ca737fc6ce5d275f9d`，确认安装/发布前复核、失败及局部析构、原取消/真实完成/关闭边界保留。

总控本轮两份独审原件只读复制到`inputs/root_user_init_04_configuration_independent_review.md`（SHA-256 `98d41aae7817478b632d6993f14e3d6944fa19ba610c33e19611bd516adeb4af`）和`inputs/root_user_init_04_lifecycle_independent_review.md`（`496f9afd3e7bd8ebaea100ba39c9c799a1eded93ce78facff60fa36c821eb26c`）。两者未见所审范围确定新源码阻点，指出初稿把两份补丁SHA混写的交接错误；本交接已分清原件，收尾逐文件块核其仅顺序不同，不改任何送审源码或补丁。原03问题报告另存`inputs/root_user_origin_independent_review.md`，SHA-256 `aedcf6091f5fba16c418e28a620186aa42d452913c1d4c8bd0926738e8d4367a`，也不改其历史结论。

`final_file_verification.json`和`output_manifest.json`随收尾生成；前者再次核全685前像/候选/Git回演、两份补丁块和旧03原件，后者固定本片20项实际前后源码及交接/补丁/清单/报告/工具。全685源码由各自源表逐项固定，输出清单不列自身摘要；最终交接消息另给清单和handoff摘要。

## 尚未验证与交给总控的联机条件

- 编译、QObject真实排队、Windows实际行为、RK运行、真实仪器、响应/资源、长稳和同板控制均未验证。Git准确应用和源码审查不代替这些结果。
- 仅在总控批准的原资料、原INI、已确认板卡/主控和安全运行状态下验普通开始：先核实际三INI/孔数/20或42表、默认杯/试管、账户权限及项目；确认普通模式确实采用本次Source。Ts排入发送队列、主控收到、主控接受、仪器实际运行应分别留证。
- 需核初始化前后配置变化/旧main参数冲突会拒绝且无ready；条件不足保留待验，不为此构造电脑模拟或擅自改真实配置。失败后的项目恢复已有副作用另核，不宣称全库回滚。
- 需收真实Tt/TR/Tr及CAL批号通知，确认source校验、原项目→试剂锁、setter/revision、报告/样本状态、扣退量与重复/乱序/断线后恢复仍正确；Us真实保存→构包→主控回读链一并做受影响回归。错误原因或字段不确定仍交总控，不以重复下发补证。
- 需核取消、10秒到期、账户/连接/配置/任务输入变化后的迟到实际finished不发送Ts，关窗仍等已派发Worker结束；异常来源finished不假报processed。
- 真正User写事务准入及调用者、页面采用、Emergency测试中按已发任务去重补发、草稿真实保存回读、水/杯/急诊盖前置及发送失败恢复仍缺。算法未动。
- 未新增线程/定时器。Source保留三INI解析值，原字节仅为读取/私有解析过程临时内存，外部只交摘要。新增原文件复核和已有GUI三INI/项目摘要的真实规模耗时、内存、日志增长及共享板资源影响仍需RK测量。

正式记录、计划、README及最终构建登记由唯一总控串行维护。本片交付不构成部署或板端操作授权。
