# 第12批第二链：已有平铺发布复用的Shell窄审

2026-11-04。只读Root冻结副本`workflow_next_20261103/runtime_transition_root_review_12_02/`，不跟作者可变目录。结论：这两个实际差异未发现确定阻点，允许Root继续审新核心、批准与实际生成物；不等于本报告批准生成、上板或证明准备通过。

本次10份代码实际SHA/字节均匹配快照，逐份对比01，只有prepare模板和generator变化，另8份完全相同。generator仅新增准备收据的互补标志校验，AST解析0；未import/执行任何候选。

- **已有flat：只核完整包，不动发布内容。**`-e || -L`可捕获坏链接；分支调用`check_new_release both`，既有检查要求规范非链接目录、release_id与SHA256SUMS身份相符、无链接/特殊成员、逐项SHA正确，且已固定SHA的helper进一步拒绝清单外普通文件。nested与flat的清单SHA相同，helper、主程序、报告辅助程序各自再核固定SHA。坏链接、缺件、额外文件或不符会拒绝，不cp、修复、删文件或改flat权限。元数据/阶段日志/收据的既有创建与权限设置仍会发生，不能说整个prepare无写入。
- **没有flat：保原复制后核。**条件else才在再次核材料与旧运行后执行唯一`cp -a -- "$nested" "$flat"`；随后与复用分支一样完整核两处发布、同一旧程序及旧发布，不因复制命令返回就生成成功收据。部分失败现场保留，不自动补拷、覆盖或清理。
- **事实标志互斥。**两变量初始0；已有分支仅ALREADY_PRESENT置1，缺失复制成功分支仅CREATED_THIS_PHASE置1。收据两者仅可能(1,0)/(0,1)；generator的check_receipt新增严格二选一校验，拒绝缺值、(0,0)、(1,1)或其他值。FLAT_PREPARED只表示完整检查后就绪，不冒称本阶段必定复制。
- **留驻守卫未退。**持store锁，prepare不取life锁，不要求quiet，不stop/start/current切换。开始前和结束前仍核同一C7旧run/PID/ticks/UID/exe、七配置原文、三指针、原主控及相机/3A和服务身份；emit_receipt再核材料、环境与旧程序。运行期相机若再次自动换代仍保守拒绝，不以本次复用逻辑放行。
- **后续程序策略不变。**stop、activate、launch、observe模板逐字节同01：正常关闭旧版只一次，非0/pending不重试、不强杀；真实旧0/clean1和quiet后才切版；唯一新版supervise秒数0/空session，父锁释放后只等身份；观察超时只工具返回，不自动关闭新版，不操作原主控或相机。

Root通知新02将重新metadata起步，以新核心/阶段许可重新绑定相机等真实身份，以及旧core3446、旧prepare2980、同包2834/发布6922的来源。那些值本次不是已审实际新输入；不能将旧metadata/preflight回执或stop79当新链完成。旧链正常关闭前被拒绝、C7仍运行和无旧停原件仅按Root通知登记，本Agent没有联板复测。

| 本次固定对象 | 字节 | 完整SHA-256 |
| --- | ---: | --- |
| root_code_snapshot.json | 3060 | `cadf9eb89191f2a1327e30dc7e6cbcacefde24a01c6a59c844747fd87872a56f` |
| phase_prepare.sh.in | 892 | `7ec12ed530465f193843aff83b47d906ddd4faeda6d17b4bff7ad31817b13dfe` |
| generate_runtime_controls.py | 35636 | `0322264fbf1c7a7439f86fa04381ef3695c70d4e5c128aa5f78ff35334d66a4e` |
| runtime_common.sh.in（未变） | 14211 | `dd304e7463482eec57b2af07443115fc0e176eff9014c431e7c4607988cd8900` |
| 原01快照 | 3944 | `f6163b5a3fb07d4530ee49430ec92e4763b974a61c7da14f7de1800d39b40e27` |

本次只做字节diff、必要只读检查及Python AST解析；没有生成、source、phase执行、产品、编译、SQL、SSH或板卡操作，没有改候选或正式文件。写后再次核快照与10代码稳定，实际新控制脚本另审。
