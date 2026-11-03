# 06 批次网络中断后续做暂存：独立只读复查

2026-11-03。结论：对固定恢复工具及这一次实际暂存证据，未发现范围内阻断问题。原无线失败保留，随后通过同板有线入口重用已上传的同一包完成独立版本目录暂存；证据没有把它写成切换版本、启动上位机或业务通过。

本次只新增本报告。未执行恢复/部署/检查脚本，未重新上传、联网、连接板卡、访问业务数据库或运行产品，未改正式源码和既有证据。此处所说的板端实际结果来自总窗口已经回收的记录，不是本窗口再次执行的结果。

## 固定对象与指纹

- `resume_stage_06_wired.py`：SHA-256 `c50020b25d203d6d697eddb7f5f3b7b2da76c8353efe9e6f87c551cb90fc3c76`。
- 它发送的原 `stage_board_b_release_06.sh`：SHA-256 `8af975382001058b0eb0d52b2f021d992cad4199cfd0b9bbbc26d7f6ae944809`，没有换成另一份暂存脚本。
- `integration_06/stage_network_recovery/started.json`：`4c564a7e6b1cb09adb6d452829bfb269d788b0839b9d44b73c21abd9eafe1409`。
- 同目录 `complete.json`：`3ed80114ad19a2fa531edb8c08e73bb7081c0888a152c715224b37647a4130b8`。
- 完成记录中的 9 项 inputs、5 项 results 及 started_record，独立重算实际文件 SHA，15/15 一致。started 的全部原字段与 complete 一致，complete 只是另加完成事实，没有修改原 started。
- 本地 103,938,278 字节程序包的实际 SHA 为 `d454ca929dcb77c1c5e41b18f84f29cb48cc52d66d3d2d01858f204478718b2d`，与原构建记录、原 stage_started、有线检查、恢复预检和暂存日志相同。发布编号为 `14ce9714aaa0017c7ad78ff3f551d978672e5def79b96e817605295d7e59020c`。

## 恢复动作与实际证据

1. **原失败没有被覆盖。** `integration_06/upload.exit` 仍为 0；`board_b_stage.exit` 仍为 255，原日志仍为 `client_loop: send disconnect: Connection reset`；原 `stage_started.json.complete` 仍为 false。无线检查 01 也保留连接超时和退出 255。有线检查 02 是另外的文件，退出 0，不能反写成首次无线成功。
2. **目标按板卡身份确认。** 恢复工具 18–20 行对两个 SSH 调用均使用 StrictHostKeyChecking=yes、HostKeyAlias=rk3576-board-b 及板 B 专用 known_hosts。只读重算该文件内 ED25519 公钥指纹，实际为 `SHA256:S4ihaw0V/Z8w7v3VlJOQEFLEH3Hp6nr3KjiUTxanwPg`，与项目板卡身份记录一致。恢复预检及原暂存脚本均实际读取并断言硬件序列号 `8314b1e8ffd0321f`；有线检查、预检、暂存日志均记录相同序列号。因此不是只因 IP 为 192.168.100.123 就认作同一块板。
3. **只重用原上传包。** 工具 42–50 行要求原发布/包/脚本身份、上传 0、暂存 255、有线检查 0 且无成品或临时 stage。71–80 行再次检查序列号、旧 current、无目标 release/临时 stage 和原远端上传路径的实际包 SHA。随后只经 SSH 把原脚本和原上传路径交给远端，没有 scp、重新打包或重新上传的分支。实际有线检查报告包大小和 SHA 正确、FINAL_RELEASE_PRESENT=0、无 STAGE_PARENT；恢复 preflight 的实际退出为 0，记录 EXISTING_UPLOAD_HASH_OK=1、NO_STAGE_OR_RELEASE=1。
4. **远端写入范围限于本次暂存。** 原脚本核 root、序列号、包实际 SHA、部署根和 releases 非链接后取得 lifecycle.lock，再创建 releases 下唯一临时目录。解包后核 release_id、SHA256SUMS、完整发布清单与动态库，改本次版本目录的归属并移动为新的 release。成功清理只针对本次临时依赖文件、已空的临时父目录和本次原上传包。没有修改 current 的命令，也没有写原运行资料、INI、数据库或主控目录；没有启动/停止上位机、主控、服务或占用显示的命令。
5. **引入生命周期脚本没有调用启动入口。** 包内 `rk_board.sh` 仅在 `BASH_SOURCE[0] == $0` 时执行 main；本次以 source 引入，只调用 verify_release。实际包内脚本 SHA 为 `e3587bad320003bf1a2ea7b3176bf14d23527ebaae65b60ffb4bd5388d3866d2`。ldd 属于文件依赖检查，不能当作上位机运行或界面验证。
6. **完成必须有本次真实结果。** 恢复工具使用新建恢复目录及排他创建 started/complete，不覆盖旧恢复结果。预检非 0 就不发暂存脚本，暂存非 0 就不登记完成；实际暂存 0 后仍要求准确的板卡、发布、包、current、ACTIVATED=0、PROGRAM_STARTED=0 和批次整行，拒绝 `not found`，并复核原输入指纹未变，才写另一个 complete。实际记录始于 01:33:55，完成于 01:34:00；preflight.exit 和 stage.exit 均为 0，complete 明确 original_stage_exit=255、actual_resumed_stage_exit=0、activation_performed=false、program_started=false。
7. **实际产物与旧 current 相符。** stage.log 报告的新上位机 SHA 为 `f3e814b4110b14ddbaef8c4516ab8a5b1c43747c45dcd4fff54ad3af0806ec6c`，报告渲染程序为 `0049f525076c24d4fd3c7a6415edb447431bf8f60e816708c1403d5c436942c0`；独立核本地产物与 build_complete 一致。CURRENT_RELEASE 仍为 `releases/e31491a11e2e51764ab49c2c4435ce2f6f1f136223a76323d508ed613a758270`。有线检查时主控仍为 PID 79082、监听 5000；本次记录不能进一步证明后续长期运行或真实仪器状态。

## 结论适用范围

本次可以登记为“原无线暂存中断；另一次有线恢复暂存实际完成，未启用、未运行”。不能登记为原工具首次成功，也不能据此认定新版本在 RK 的功能、界面、数据恢复或真实仪器业务通过。

collector/finish 正在增加的严格恢复证据分支不在本次结论中，待作者固定后另核；不能在尚未审定该分支时，把本报告自动扩展为最终归档收口通过。实际启动、资料/设备回包核对及资源测量仍由总窗口按获准范围安排。

## 追加：收集与归档收口工具的网络恢复分支

本轮固定 collector SHA 为 `a7bf5381fcaf107592795ea81d965d2a65cccf9d8d2e496f159cfdd62e851f94`，finish 为 `1d2edbb9807121a6c4c03f0c456f9c3c307301dc1826ef70e9e77b4e977aab78`。旧版 `.before_network_recovery` 均保留，实际 SHA 分别为 `e2b128251a15c9257b0dcc81bfd12dce91868b212ecb9ba540018bfa4620e080` 和 `60bc7f1aa0b202fb499b7be8668a5f44a9197bb3379d0d73c837c19fdb840585`。本窗口仍未执行工具。

发现以下 P2 阻断，已通知总窗口；不能把这一版收集工具判为通过。

- **未建嵌套目标父目录。** collector 344 行只创建证据根目录，345–346 行直接 copyfile；恢复分支返回的目标有 `stage_network_recovery/inputs/integration_06/logs/...` 等多层路径，正常显式恢复分支会在复制时产生 FileNotFoundError，并留下部分证据根目录，使下一次执行又被 274–275 行拒绝。总窗口随后报告，先前已开始的首次实际收集确实在此退出 1，已停止且未运行 finish；没有把这次失败改成成功。本次只读检查也看到了证据目录内仅有前七份构建/晋级文件，恢复原文与完成记录尚未形成。
- **同源字典覆盖了原失败的根级归档位置。** 285 行 `copies.update(network_copies)` 以源 Path 为键，把原 `board_b_stage.log/.exit`、`upload.exit`、`stage_started.json` 的根级目标改成嵌套目标。即使只补父目录，finish 56–59 行所说明的根级原失败文件仍不会存在。修复应保留原根级副本，并把恢复证据另作独立循环、逐项创建父目录；不能只补 mkdir 而继续覆盖目的位置。

除这两点，窄审未发现第三项独立确定问题：collector 87–177 行在原退出 255 时必须显式提供固定 `--stage-network-recovery-sha256`，并核固定完整输入/结果集合、started 与 complete 对应关系、真实文件 SHA、精确路径、同板同包及预检/暂存实际 0。缺字段、缺文件、换路径或少链会拒绝；只去掉 provenance 中的恢复字段不能把原 255 转成首次成功。finish 18–33 行要求归档恢复摘要与验证记录相同、collector 与归档原文相同，并重新检查恢复链及归档原文；无恢复摘要时要求根级原退出为 0。说明文字分别保留首次 255 与恢复 0，继续声明未切换、未运行和业务待验。

本段结论停在上述两项修复及部分证据目录保留工具待固定复查。恢复暂存本身的先前结论保持；收集失败不代表板端暂存失败，也不代表正式程序包损坏。

### 两处复制修复与部分目录恢复工具的复核

随后固定 collector `d71aa80c6681b5fc8f1594b953c90c98fe56a2df7bbcbdafe9f4991cb0e95bea`，原 a7bf 完整保存在 `.before_network_copy_directory_fix`；目录恢复工具为 `c961888550131b73ebaeddb87a59cc77d810729cbe604070fe6bd63a595cfdfd`。这两份源码指纹均独立重算一致。本次仍没有执行它们。

两项原复制问题已修：原 copies 不再与 network_copies 合并，同一来源的根级原失败与网络链副本分别复制；两个循环都先创建实际目标父目录。

恢复工具本体的限定检查成立：只处理固定 `04_references/sn100_host/workflow_night_20261102/integration_06`，目标是同父目录固定 `integration_06_collector_incomplete_01`；先拒绝已有恢复记录/目标，查路径和链接、恰好七个单链接普通文件、各自固定 SHA 且等于原输入，没有完成态或额外内容才继续。只读重算当前七份部分文件，均匹配工具常量和各原来源；目标、恢复记录及首次缺失父目录均尚不存在。失败返回转存件实际 SHA 为 `c7c892a190de7680d26faf9dbe148ec72f016b539bf7fb96346ba6f3e39bdb12`，对应 exec_command/chunk 3ae95b/实际退出 1；记录明确是根保存的真实工具返回，不伪称 collector 自产日志或重跑。工具先排他写入并落盘 started，再次核七份原件和目标才在本次 Windows 执行路径改名；改名后再核七份，最后排他写 complete。中途失败保留现场，不自行回滚或重复执行；不创建新的完整证据目录，也不执行 collector 或板端动作。

**新增恢复收集链仍有一项 P2 完整性缺口，已通知暂停。** d71 的 354–358 行只遍历 complete.partial_files 现有项，未核恰好七个固定键和各固定 SHA；少一项时其余六项可以通过，最终少归档一份原件却登记 collector_directory_recovery。359–360 行只把 started/complete 加入复制，没有核两者仅完成/改名标志不同；started 缺失也要等新 destination 建立后才在复制时报错。preserved_directory 只检查位于父范围内，未要求上述唯一固定目录。应在创建新证据目录前核固定七份、精确保存路径、started→complete 关系和全部来源存在，再归档；这一项属于本次“七份失败原件完整保留”的检查，不涉及业务或板端扩大。

finish 后续仅说明文字修正为 SHA `69334d940df24609eaafbcb4fa4516ea689d745d6cf5634a2ecb7c150e849367`，已复算；Qq 说明为按原声明数量读通道值，N 为液面回报，另说明首次 collector 退出 1、七份改名保留和真实工具返回转存来源。新 collector 尚未实际执行，README 中关于其退出 0 的文字应由总窗口在实际新收集成功后才生成，不能用这一段源码文字充当运行结果。

### 最后固定修正的收口

最终固定 collector 为 `bd5d199ae648a9ee8c7db802baa332e89a22280736cbc7ec90861f1c4c1c193a`，恢复工具为 `370008a7f777c43af47031e88dc5f3fc5060c0eca0e8cb7b7d6fc6e87803985b`，finish 仍为上述 69334d。三份实际文件 SHA 均匹配。把恢复工具的 FIXED_TOOL_SHA 换回前一固定值后重算，恰等原 c961，确认只同步这一常量，其已核改名行为没有另改。

本轮仅核上述 P2 修点，结论为：源码修复核对通过，范围内不再有阻断。历史失败和各轮 P2 保留在前文，不能删成首次通过。

- collector 354–369 行要求 partial_files 与固定七键、七个原 SHA 完全相等，原路径与保留路径必须恰为本次两个固定目录，并核实际改名及没有旧完成态/新证据目录标志；缺项、加项或换目录均在建新证据目录前拒绝。
- 370–390 行核恢复工具、旧 a7bf、真实退出 1 的 c7 转存件身份；runpy 使用非 main 名称，只载入定义，并只调用读取检查函数。started 与 complete 必须仅 complete/rename_performed 两个标志从 false 变 true，其余全文值相等。check_partial 再核固定保存目录的完整七份、单链接、指纹及原来源。
- 397–401 行在 destination.mkdir 之前，对所有将复制的基本、网络、换行恢复、目录恢复、审查及脚本原件检查存在、无路径链接且为单链接普通文件。402–411 行仍分别复制原根级证据与网络副本，逐项建父目录，没有退回 Path-key 合并覆盖。

本窗口没有执行恢复、collector 或 finish。总窗口可据此按既定顺序执行恢复改名保留、显式恢复指纹收集，再核实际退出与归档结果；本节证明已报源码问题收口，不替代该次真实执行结果，更不表示上位机业务、界面、资源或仪器通过。
