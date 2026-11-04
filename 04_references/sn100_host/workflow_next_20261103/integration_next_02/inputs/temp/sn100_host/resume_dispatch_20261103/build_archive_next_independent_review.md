# 下一批构建、保存与切换模板：总控独立窄审

固定读取时点：2026-11-03 19:30:59 +08:00。对象为 `temp/sn100_host/workflow_next_20261103/build_archive_candidate_02/`。收尾时尚无handoff.md，作者仍在收口；**本文是所列字节的接口/关键代码审查，不是终版晋级或动作许可。** 最终交付后总控须比较下方SHA，变化的源码再窄审。本Agent只创建本文，没有改作者/正式文件，没有执行阶段、构建、产品、网络、板卡或显示操作；只执行了AST及bash语法解析。

**结论：文件阶段的false门、完整重编译及对象绑定、只保存新目录的锁/活体保护、包后单向启动意图与总控留屏裁定v1相符。切换草稿仍有一项P2：无图形进程/服务/显示占用的检查不在取得生命周期锁后复核；冻结实际切换脚本前应补齐。** 本批源表、对象数、包、发布号、上传/暂存/切换和观察结果均未产生，不能填为成功或实际退出0。

## 切换草稿需要补齐的检查

`transition_release_next.sh.in:163–177` 在申请lifecycle.lock之前检查无GUI/报告/检查进程、sn100-shell.service为inactive/failed、VT7/DRM未占用；`:178–180`才取得生命周期锁。锁后`:181–189`重核指针、包、凭据、配置和主控，但没有再核上述图形唯一性/服务/显示条件。

具体窗口是：早期检查结束 → 已在启动交接中的服务或监督进程变为活动 → 生命周期锁竞争/父子交接后本脚本取得锁 → 按早期“无GUI”结果切current。不能把早期快照或“拿到锁”本身视为此刻所有图形/服务/显示条件均成立。唯一总控串行安排可降低此窗口，但模板的“切前核无GUI/释放显示”证明仍应在锁内成立。

建议把这些只读检查提成共同函数，在持 `immutable_store.lock → lifecycle.lock` 后再执行，并在原子切换前最后复核；失败只返回，不停止进程、不清显示、不回滚current。原模板仍默认拒绝，不能为修此问题增加自动停/启动器。本问题在02中仍存在，本文不将其称作02相对01新增的回归。

另一个需随实际脚本加强的唯一性细节：`:63–77` 的check_controller只统计精确 `/userdata/shared/ACA_V1.0OffLineD-500` 路径，并以grep确认至少一条5000监听含该PID；它弱于本候选stage的按同basename（含deleted）排重及逐监听行归属校验。第二份同名不同路径/已删除exe可能未计入。建议沿stage同一口径拒绝这些不明进程，并核所有5000监听都属于唯一原主控，保留前后boot/PID/start_ticks/exe/SHA，而不只比路径。

## 九份工具相对01的实际窄差异

已读实际diff，并在AST后复核九份SHA；这九份在本次检查前后相同。没有把01或09旧业务/板端结果借作本批结果。

| 文件 | 相对candidate01 | 所审关键点 |
| --- | --- | --- |
| `build_integration_next.sh` | 仅WORK末级01→02 | preflight在mkdir WORK前，拒绝已有WORK；unset两项对象复用变量，使用新arm64_build，全量编译及检查两程序再打包；失败不部署旧产物 |
| `verify_integration_next_inputs.py` | 字节相同 | 核正式完整最终源表、实际五类输入、构建前后身份；由实际source/Q_OBJECT/QRC/helper推算对象集合，不固定旧290/2或684 |
| `integration_next_object_binding.py` | 字节相同 | 识别冻结build规则及实际输入，逐对象/生成物SHA和ARM64 ELF，实际集合必须相等；不把重建的直接输入绑定冒充编译器.d或全头依赖 |
| `integration_next_tool_support.py` | 输出路径02、cfg v2及观察裁定、stage路径/批次02 | 固定Root裁定record且须入evidence，cfg只含完整构建/只保存许可；重核真实构建/对象/暂存原文和SHA |
| `stage_board_b_release_next.py` | 批次/远端路径/标记01→02 | cfg/allow_stage、实际build/package、固定SSH指纹和known_hosts前后SHA，保存真实两次SSH退出及板端guard原文后才complete |
| `stage_board_b_release_next.sh` | 远端命名空间/末标记01→02 | 只持immutable_store.lock，唯一旧应用/主控及三指针摘要前后相等；不写current或运行资料，不启动/停止主程序/主控 |
| `archive_integration_next.py` | provenance批次01→02 | 先核本批实际build/stage，再建新恢复归档；完整源码包/程序包回读、对象绑定原文保持，功能/仪器仍待验 |
| `verify_and_save_integration_next.py` | verification批次01→02 | 重新读实际对象与三处绑定原文、真实阶段终态和清单，记录0的前提是实际build/upload/stage核验已经成立 |
| `finish_integration_next_archive.py` | 字节相同 | 归档/证据完整清单、LFS属性、对象绑定与阶段再次核对，只新建文件阶段完成记录，不回写旧清单，不声称新应用已运行 |

小的标记遗留：stage Python `:349` 最后仍打印 `BOARD_B_INTEGRATION_NEXT_01_STAGED=1`；其真正stage记录、远端末标记和公共核对均已是02。这不是本次失败即停/当前程序保护的阻点，但终版应修为02，避免人工读日志误判批次。

## 配置、包和运行意图没有相互循环

当前cfg的frozen=false、allow_full_build/allow_stage=false；真实源表/元数据/current/主控路径/工具表均为空。support.cfg首先拒绝未冻结；构建在创建WORK前调用preflight，Python阶段入口在输出或SSH之前调用cfg。空值也不能通过后续格式和完整元数据核查。板端stage.sh是经已核Python入口传参的内部脚本，不在板端读取PC的cfg，实际执行须继续只由这一已冻结入口发送，不能把手工传参运行当成false配置下的授权。

源码表和工具配置先冻结 → 新完整构建实际生成binary/对象/包/发布号 → 文件阶段完成 → Root另建runtime_launch_intent。这是单向引用。release_id由包含binary的包内SHA256SUMS产生，因此不能编回同一binary；新环境名 `SN100_LIS_OBSERVATION_DIR/ID/RELEASE` 只在核包后由Root传入。cfg SHA已进入build_complete，不能包后修改cfg的许可/evidence/tools、再改完成记录补洞。

`runtime_launch_intent.template.json`目前frozen=false、实际身份和路径空、四项后授许可false；其中supervise_seconds=0是预期监督策略，不是实际进程退出码。意图引用既有cfg/build_complete/package，不含自身SHA或切换脚本SHA；外层交接可以分别记录二者，不能再反向放回前者。切换草稿直接核意图固定平铺JSON的字段顺序/类型、实际版本及SHA、Root、0监督、私有700观察新目录和动作许可，不只比较文件存在/SHA，也不eval/source意图数据。

## 保存阶段对现有程序的保护

stage Python固定板B serial `8314b1e8ffd0321f`、SSH别名 `rk3576-board-b` 和ED25519指纹 `SHA256:S4ihaw0V/Z8w7v3VlJOQEFLEH3Hp6nr3KjiUTxanwPg`。仅接受专用known_hosts中恰一条该已核key，StrictHostKeyChecking=yes且禁全局/DNS/自动更新；板端在任何写入前及锁后再读序列号/current。没有仅凭IP认板，也没有读取旧PID当活体。

上传、解包两次SSH各取immutable_store.lock；两次之间重核本地输入/公钥原件，再核板端current。解包阶段不取lifecycle.lock，旧监督器可继续持有它。写入仅为随机上传、新私有解包目录及 `releases/integration_next_02/<新release>`；成功后只删除自己本次上传/空临时目录，不清其他temp或旧发布。失败留下本次现场，不覆写重跑、不发TERM或stop。

板端next_live_guard读取22字段：boot、current、唯一应用及主控的PID/有效UID/start_ticks/exe/SHA、lckfb实际UID、原主控逐行5000监听归属、三份资料/配置指针原文SHA。每份快照读取末尾复核，同一存储锁内前后原文必须全等。PC保存板端base64解码原字节和SHA，不自行生成活体JSON。范围严格为locked_unpack_and_commit，未证明上传前后整段业务/界面观察，也未据三指针SHA声称七份实际INI正文指纹都已测量不变。

stage.sh会source包内rk_board.sh以复用verify_release；本地先核完整实际包及构建终态，板端先核清单/release_id和文件，正式helper只有函数定义及直接执行guard。其来源不是调用启动main；未在stage路径看到自动停止/启动产品。Root最终冻结时仍须把实际包/helper SHA作为固定输入核准，不能把任意新helper当成可信旧helper。

## 切换、启动和留屏的边界

模板第一道 `template_frozen=false` 直接exit78，未改此行的原件不会进入板端操作；其余空值和许可还须分别通过。prepare-flat仅在存储锁下从已核嵌套新包复制到不存在的平层新目录，保留原件和current。activate-only要求Root真实批准/旧停止凭据/包后意图，以及旧程序确已退出和释放显示；锁顺序为store→lifecycle，末尾显式释放两锁，不拿父级锁启动子监督器。上文P2需补后才可冻结此实际路径。

模板本身无stop/start/TERM/超时清理指令，切指针成功只返回 `PROGRAM_STARTED=0/LIVE_HANDOFF_PENDING=1/START_OWNER=root`，不记录新程序已启动或leave-running通过。模板没有SSH连接功能；将来Root实际发送/启动时须另用已核SSH指纹与序列号通道，不能以板端serial检查代替SSH主机真实性。

Root独占新启动；状态窗口只做同进程被动CPP、只读等待和解析。候选说明已如实列出现有supervise 0仍有监督异常/信号TERM路径，旧start 0另有30秒READY超时TERM，不声称复用后任何失败都不关闭。真正唯一启动器、观察环境的实际传递、服务/显示/用户及运行锁交接仍待Root另审，本候选没有交付或运行它们。

根裁定v1实际SHA为 `c27714ee5142d4125d40235da2f551977d5e9dc4580885e34ef8755f0242be3f`，与cfg/意图模板一致。文档对内部观察无进程退出码、application_exit=null、三项退出/关闭评价false、等待器/解析器分记真实退出、观察失败不关程序等语义一致。CPP/结果/ready本片没有生成，不能据文档当作观察已通过。

## 本次静态检查与所审SHA

实际执行的是WSL Ubuntu-24.04/qyq中的7份Python `ast.parse`（未import候选），整个AST命令实际退出0；以及下列三份 `bash -n`，各实际退出0：build_integration_next.sh、stage_board_b_release_next.sh、transition_release_next.sh.in。没有执行任何脚本正文、preflight/阶段函数、构建或产品。只给这些静态命令登记0；本批构建/上传/暂存/切换/启动/等待/解析/应用退出均未执行，实际退出为未知，不补0。

| 所审文件 | SHA-256 |
| --- | --- |
| `archive_integration_next.py` | `b127a25b65d3d223f09273c113a4487b065d8a1b6412bbdbf4b33b63752fc6e4` |
| `build_integration_next.sh` | `151ec4bb75936d608f80f27dc606a610067b32a6aa4cd1ec0e4bb862996cfa7b` |
| `finish_integration_next_archive.py` | `acaccfd0661cb7f90bf8eb64c997108f9cd9e412dd0af38d823639300bc4bbc2` |
| `integration_next_object_binding.py` | `322e24feb600903de6ea35d791bdf85aba064206e9e234bb33c2b758eb3e0497` |
| `integration_next_tool_support.py` | `ad2cdcdd3e04150a246984f11656f4d1303f5d1234d87cdcac6e9398d3045855` |
| `stage_board_b_release_next.py` | `13b1f7535fa1786ea13e1ba5fae966b8c458e79c70183a31e1975cce63199e52` |
| `stage_board_b_release_next.sh` | `8f130d868bc6e8e2ad4d33abad560f117df18aaae6076362d89e081d75bdf8ef` |
| `verify_and_save_integration_next.py` | `b35591f3d1e96cae5b686cb3b3e7f6bee7e28d975b5c1d34c523cd5e4a233928` |
| `verify_integration_next_inputs.py` | `26d43ce36eb6f39b4ca91de2e24adb72d0c2ad3751a4dd1502e6331d5065fdec` |
| `transition_release_next.sh.in` | `43e9eb462ab0a62bc052a311f4970b3d048ccfc45e08a56e2cdb37e15cbdd9c1` |
| `runtime_launch_intent.template.json` | `9e7a226d2960474026e781d74c651fc049519372e066fe29932d987a18ca5d16` |
| `integration_next_tools_config.json` | `a0d17377e1636081f31ebe1ff94a31d76b7926e14e4a42f8288dd8aa0309c99c` |
| `operation_contract.md` | `74ee136577b60cc4349ebc4b1abf25bf04c05ef07646f4394a33fa0518aa704c` |
| `root_decision_alignment.md` | `a490f0b0070ed03b7fd2e144bee4f665fc922712f5e4cef02d9c084fb9c4aae5` |
| `README.md` | `6661f5768804c7fffa5c67a7fe542fd77e6b8bc03b4313cf6a4c743b7f6a992d` |
| `basis_inventory.json` | `480cfe83e583e923d8840a88c18768ce922754bc87106ace710dee1563058ddd` |

README中的“684当前正式等09”只能作为作者起始底稿确认，不能充当收尾现场事实：Root已先合ASCII两文件；本次没有重核整684源码，也没有将其他窗口未验候选当作正式。终版应注明该依据的实际时点。实际源码、Root合入终态、完整源表、工具SHA和许可冻结后还须重新核齐；本报告不批准现在执行false模板。
