# B1窄合15：空架注册路径独立源码审查 01

日期：2026-11-04。审查人：独立子代理 `/root/origin_audit`。

结论：本轮以15为主的空架路径只在实际snapshot.append成功后增加LIS内存修订记账，未见新增项目/INI/数据库读取、保存/恢复动作或业务门槛。当前孔数、传入容器和98个本地架名范围保持15原代码；这不是这些继承范围已与Windows全部有效行为等价的结论。

## 冻结字节和独立组成核对

目录：`temp/sn100_host/lis_repository_B1_on15_20261104_01/`。下文候选源码相对于`review_01_source/06_programs/sn100_host/`。

| 对象 | SHA-256 |
| --- | --- |
| `review_target_manifest_01.json` | `5a3a1dc9a3b0437ec382cc00d9fefd96977fdcd65aee8a44bb8b2abd7c7fb6b9` |
| 三源实际后像 | repository.cpp=`72687dcc481133e786898359a41f735be024eedd51336b647620774b93942ab5`；repository.h=`bd1aabffba4cc0f9f27d42a6e2dd1bdaffe16c632a0b115e1387c025a63d4438`；types.h=`1964b396bf4236fcf2e9a904bd37cf3a874a7b33bc65d582354d02a1780f802d` |
| 15前表`before_source_manifest.json` | `f3519bacb0efca4077ef93ef21f59e906021585dbd9ae414a37e7bb9b2abf6d0`，697项基准元数据 |
| 15目标前像 | repository.h=`5f85000cf06acbeace7b73b22e3a17616a551bf7688782b0cc7ef8ef9b207705`；repository.cpp=`1453a07f669564f2f353653b6e0d61c48f2cdca827f21ed7421e9373e8ef66df`；types.h=`d93e30392e17ebacf45bcc9726ac142b500bdebdb2581acabd39b2e3fc470a46` |
| Root输入 | `inputs/root_B1_review_01.md`=`37cb409a0a7df0ba95e54eb5c0fbb5a9fc770a7fb52c24f5f19e8780005f4afc` |

实际三源逐SHA与目标清单一致，0不匹配。没有执行作者的composition/check/merge脚本；直接读实际源和git diff，并在内存对文本做精确剥离后重新算UTF-8字节摘要，独立得到：

- types完整等于原B1第三轮。
- 新header剥离15的空架声明/注释及initialized_字段后，字节摘要等于原B1第三轮header。
- 新CPP剥离15完整register函数和两处initialized_赋值后，字节摘要等于原B1第三轮CPP。
- 新register函数只去掉`noteLisRackPublished(rack);`后，与15实际原函数完整相同。

因此原B103不可复制仓库、LIS来源/映射及一次应用内核没有被15完整后像覆盖；15空架方法和initialized_也没有被旧B1后像覆盖。旧B103报告仍为`1f749bb0c3e4b0fee06ddc642fcaa87721a5d74c62823b3565c709941649c0c5`，原件未改。

## 空架实际路径

`original_sample_repository.cpp:491`的registerEmptyOrdinaryRack仍先核：initialized_已成功、输出指针存在、本次ordinaryHoles_在1至128且调用方孔数与它一致；nextRackId至少2且未耗尽、不与现架id冲突。再从S01至S98寻找第一个未占用名称，找不到按原文拒绝。

只有上述条件全部通过，才逐新孔构造SampleHole，按调用方本次containerIsTube选择Tube=0或Cup=1，分配原内部rack.id并append到同snapshot；528行此时才调用noteLisRackPublished，之后按原规则返回rackId和snapshot及true。所有原显式失败分支都在该行之前，不记一次假注册修订，不新增通过saveRack/load换取身份的路径。

noteLisRackPublished:686只调用nextLisRevision、写本架修订、清并填写本架各孔修订。nextLisRevision:676只检查本地计数/可用性并递增，不回绕；耗尽时将LIS修订标不可用并返回0，不将原空架成功改成新的业务失败。该链没有项目/INI/SQL、ProjectRepository::load/recoverPending、originalLisProjectSource、信号、回调或整架空闲门调用。

initialized_入口false、完整初始化成功后true的15规则保留；它不被lisInitialized_替代。register本身不新增设备空闲判定，也不宣称独立核过设备状态。保存/恢复及上层空闲接线仍属于15原流程，本轮三源没有改其调用者或用新增读取代替既有门槛。

## 继承范围和验证限制

- 孔数是本次成功初始化记录的ordinaryHoles_；注册时不重读后来变化的INI。传入孔数不符仍拒绝。传入当前容器选择只作用于真正新孔，不重建旧架/急诊架，也不重读瓶型/项目来补默认。
- 本地命名范围仍是S01至S98，满98时拒绝，且保留1至128孔及内部编号耗尽检查。这些是15实际继承事实，未据“本轮没新增”就认定为Windows/实际配置的全部有效范围；有效范围仍交Root按原配置/原行为核验，本报告不擅自放宽。
- 新SampleHole包含旧B103既有的lisStartedAt默认当前时间；成功构造新孔会读取该时钟。结论限于没有新增项目/INI/数据库I/O调用链，不是“没有任何系统调用/I/O”的测量，也不是响应或资源通过。
- 新架LIS修订只证明本仓库实际append及目标身份，不能作真实设备会话、保存许可、患者应用、报告预留或仪器执行证明。B2/B3、共享任务/User摘要及生产消费者仍未接。

本审没有执行合并/普通patch或构建工具；既有三方工具与封装退出原件保留，不从封装摘要推导产品或普通patch失败。本轮只新增本报告，不改冻结源、旧B103、Root正在构建的15或当前正式源；没有编译、产品/测试、SQL/模拟、网络或板卡操作。源码组成成立不代替运行验收。
