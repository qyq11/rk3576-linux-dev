# 第六轮八片串接：共用入口独立核查

结论：按本次固定八片隔离源码，未发现新增确定交互阻断。意图 v2 delta 没有覆盖 CAL、UP及批组的复制写保护；Uh收尾、Qq尾字段和项目重读之间未见互相制造成功、误关主窗或新清 Us/任务未知的接线。此结论是共用链路窄审，不重复各片完整审查，也不替代 QC 对意图第二版两项修正的独立结论。

正式05及其已有20项板端自动检查保持原范围，不能用于证明本文源06通过。这里只新增本文和两个只读临时输入清单，没有修改源码/候选/正式文件，没有实际编译、运行产品、访问业务库、联网或连接板卡。

## 本次源码与补丁身份

隔离源根：`integration_06_review/06_programs/sn100_host/`。`merge_review.json` 实算 SHA 为 `e48ec9c5d5c3ffa9ee63f248dad9482536aebc7a48b871c6e00b1941513a13fa`；它声明基于05的639项源码。05底稿清单 SHA 为 `e785702f907aecee9bc645614f122c05f1c6053d33ecb1b94f7e08ee8eaaf111`。

按元数据逐项实算，8/8补丁 SHA 相符，顺序如下：

| 顺序和用途 | SHA-256 |
| --- | --- |
| Uh通知v2累计片 | `bdae1b3d7a0220ac77ff588e515988b2e9687f5a1689864653c95edf0445465d` |
| 项目意图v1 | `250b88f58b0e10b365aa3ae4a0c0005eef2e3619676e6fe659b607aa7822963c` |
| Qq声明数量后尾项 | `9d53133db944e6ce430c50cd926933ef16ef263d1f3e4291efb0117984cc64a9` |
| 项目复制核心/页面v2 | `a4ab1ade63b6bb558bb856874f36718ffb1ef65eaad7ad11af1b8de322656ef8` |
| 复制构建接线 | `9e57b4817616ae7d7220dcf47c6c4e419312787c195ab12465e540a8ce011e76` |
| 外部项目写入口保护 | `6c49a885f73671b5f732c38a734121dab011b27ed229c8b658f598ea23e7001b` |
| 根CAL写保护桥 | `eb048b9d6bd69b445256cf7e3a012aecffe5251e35980ddd0ab20c0d5b8cc351` |
| 项目意图v2四目标delta | `68b5f47ece7f544a8200a792ec15ee8baaff2876a9de93911f9ce3139eb9bfd9` |

此次按打包脚本 `snapshot()` 的相同目录/额外文件范围生成 `cross_patch_source_snapshot.json`，固定649项源码文件字节，其 SHA 为 `9ce1794b23ac219ff1a04b8d28bc1aeacb7e4f670718c955bb6dc65a1f7d459e`。这是本次审查生成的临时源清单，不是已构建/发布包清单。新文件有LF/CRLF格式变化，不能把上游候选 SHA直接当作隔离源实际 SHA；本文清单绑定实际字节。

收口时再次逐项核649/649实际源字节，无漂移。05清单639项全部仍在；新增正好为4个通道Receiver/Store文件、5个复制helper/dialog/UI文件及1个提交notice头，无原底稿目标遗漏。

关键共用文件实际 SHA：

| 文件 | SHA-256 |
| --- | --- |
| `shell/main.cpp` | `a0683da0485f71ac2b8af5abda6c7251fc35b65cc19288dd63c9d4ea1193bed7` |
| `shell/device_control_shell.cpp` | `871227756333596a198f04bd982952b2df360fa349a6bd583b3ff7a7f6bf7802` |
| `samples/original_sample_repository.cpp` | `e7cff37b2371fe76cf5ae935737dcddb08cb1e00f5a12e835d82a4101eeae1ae` |
| `samples/original_project_device_repository.cpp` | `53b80ec7646820eb65350c2685cd2f42d1e4356b117f1da384db14bc193da1eb` |
| `projects/original_project_repository.cpp` | `39fbb96c10f09fd9f51e31a046125b005413ca1cc2a5c6602e2958402898b34f` |
| `reagents/original_reagent_editor.cpp` | `56e59889bffb141aebc9e4dd6ae44a28d2f155af6dc946e2cd71a3d0c4732855` |
| `protocol/read_only_messages.cpp` | `62f581e78cadedd5e5b4a5f5d4d13e436a71f2154409c3c8bd04f9b3416e68e4` |

## 复制写保护与第二版同步

第二版delta只改同步 h/cpp、SampleStore和Root状态接收，未触碰三处写保护。再次直接比较隔离源与意图v2候选：CAL Repository仍只多项目锁后的一行 writesAllowed；DeviceRepository仍只多 helper include和项目锁后 noPending。Root、SampleStore、Sync h/cpp正文均与意图v2一致，没有整文件回退。

新初始 UP 与单项 UP 都调用同一个新签名的 applyDeviceProject，因而共用项目锁后的 noPending。CAL在项目锁后、数据库作用域前 writesAllowed；ReagentEditor在项目→试剂两锁后，仅对 project/文件 noPending。Repository自身保存/复制/删除/导入、单位写的锁内保护仍在。已证未提交和unknown清单都不能借公开只读过滤变成写许可。

复制 v2 的完整证明、同文件显式fsync、允许后来合法CAL复用旧预留号，以及readProjects/load的局部完整发布仍保留。读取失败保留上次有效输出并返回false，新UP/CAL回执不会把半份克隆列表或旧Record/SHA作为新提交快照。

CAL继续用局部target、expectedFileName、事务内版本复核；真实提交才commitSeen，写连接关闭后仍持锁取得同文件实际SHA，锁内通知保留。UP同样保留新结果对象、提交事实和同文件参数回读。守卫拒绝发生在写库前，不报提交；提交后失败仍交已提交待核链路，不回写“未保存”。先前桥报告的适配条件现仍满足。

普通ProjectStore/History/Reagent/Sample的load、snapshotChanged和originalProjectsChanged只推动读取/缓存更新，没有直接发出新的保存通知。后台originalProjectSaveCommitted来自实际CAL提交回调或显式入队已有提交事实；批组backend提交信号仍仅在对应保存入口。后续重读补显旧提交结果不等于新执行一次保存，也不自动制造第二个Up意图。普通项目/组开关生产者的后续接入范围不因本次reload而被假定完成。

## Uh后台与退出/同步失败互不混用

正常外层关窗先走原frmMain登录、草稿及告警等保护，接受后才撤销Uh并异步等待接收器stopped，再重新关窗；等待期间GUI事件循环仍运行。Uh worker只拥有通道库的短连接，不复用项目Store/Sync连接或项目/试剂QLock。

Uh批次失败/error设置originalChannelFailure并交非模态通知，没有调用app.exit、window.close或deviceControl.close。这个标记只在事件循环已经退出后影响最终退出码3，不能把一次通道SQL拒绝误接成立即关整个应用。snapshotRecovered只更新通道实际重读结果，不触发项目参数ready或第二个批次提交终态。

反向看，项目UP/本地保存失败走原业务错误/待核状态；主入口只在初始化/未就绪等启动失败条件退出，不把普通运行期间的项目重读失败接到Uh关停。若事件循环因既有启动失败等异常已结束，main会明确关闭/join通道线程，并把未消费回执保留为待核与退出3；不把未消费的提交当取消，不继续承诺后台重读。

原设备主状态/权限与通道浮动通知也保持分离：statusChanged在已有DeviceControl时不覆盖设备状态区，浮动提示仅借状态区定位，不改其设备/医院tooltip或颜色；失焦只保留结果，不补弹或抢焦点。

## Qq与Uh/权限及未知事实

Qq修补仍只放宽parseChannels的“额外完整尾字段”条件，继续只取声明count对。Uh走独立OriginalChannelReceiver，Root项目/任务入口按其命令与原接收代次分流；该比较符未进入Uh事务、项目写保护或账户权限分支。通道行仍来自原配置，未知键不增建，Base角色门槛及405/450表头分支未改，旧连接代次拒绝仍在。65535仍只可作为原始报文文字，不由本组合认证物理读数有效。

项目普通重读没有调用invalidateDeviceTaskContext，三个copy守卫也只是拒绝写入。意图v2的not-attempted-proof只按相同意图/当前连接、root/file、来源/请求、前后SHA核对对应项目未知来由，不由Uh成功、Qq接收或项目reload触发。此处分支只清对应originalProjectUpdateUnknown，不清Us/priority或任务占用/未知；本次没有发现片间接线新增其它清理入口。具体两项意图修正仍由其专门审查者核，不在此重审。

## 只列构建输入与打包覆盖

先查shell/build.sh源码：LIST_INPUTS=1跳过prepare/sysroot恢复、mkdir/构建锁、uic/moc/rcc、编译和链接，列输入后即exit。随后只在WSL普通用户以该模式执行一次，显式使用既有只读sysroot；退出0，输出仅写本目录 `cross_patch_build_inputs.tsv`，没有生成目标可执行文件或共享正式构建缓存。

输入清单SHA为 `04b74c7ccab69dff3cf069dbbecc4c0e703ca552855f27b8a701149fc03acaa9`：164条source、49条form、131条header、11条resource、18条helper_source和29条include。去重后357份实际文件均存在，均被649项源清单覆盖。helper_source会与主source重合，不能把这些行数当编译对象或编译通过数量。

新增通道Receiver/Store源码和QObject头、复制helper/dialog/UI/头均有构建输入；提交notice头随projects目录进入快照并由实际include消费。打包snapshot目录覆盖instrument、projects、samples、original_files及其它原目录，Qq文件也在protocol显式列表内；未见新增正式源/UI/头被清单漏掉。此核对只证明输入/归档覆盖，不证明后续完整编译、链接或运行。

下一步仍由总协调在QC独立审查结束后，按固定八片和逐文件一致性规则晋级、统一全量编译；RK保存、失败重读、关闭排空、提示实屏与实际仪器结果另验。本文没有扩大既有部署、权限或设备动作范围。
