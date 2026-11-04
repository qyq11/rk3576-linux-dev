# 第11批构建、存包与归档工具独立审查

审查日期：2026-11-04。审查者：Root 独立审查 Agent `/root/review_amount`。

结论：本次读到的9份代码可交 Root 按下表原字节复制、冻结，未发现新的确定阻点。当前候选配置仍为未冻结模板，不能直接执行；真实来源账、两项操作许可和冻结目录工具记录须由 Root 补齐。这里只确认静态源码边界和兼容性，没有运行任何候选阶段、预检入口、编译、产品或板卡操作。

候选目录：`temp/sn100_host/workflow_next_20261103/build_archive_candidate_11_01/`。对照目录：同级 `integration_10_root_frozen_01/`。以对应功能比较九份代码，不按新旧文件名直接判断。作者的说明尚在收尾；本报告固定的是实际读到的代码，不以等待最后 handoff 作为停审条件。

## 起止原件与实际变化

下表摘要均为 SHA-256。读取开始与收尾逐项核对，9份文件的摘要及大小均一致，没有审查中代码漂移；全部无CR字节且以LF结尾。

| 候选文件 | 字节数 | 开始及末核摘要 | 起止 |
| --- | --- | --- | --- |
| `integration_11_01_tool_support.py` | 33848 | `e7f13611666ad68fdf9054daf92e24b8030a0fa51bed9a169405df838bb8dea0` | 一致 |
| `build_integration_11_01.sh` | 4186 | `26e2cf91566afa0bc1694afeb44adec8d0a1b5f72e5d16550c85f60c4b5eceac` | 一致 |
| `verify_integration_11_01_inputs.py` | 10108 | `3abb146274fbe58e95118a4b9c7a48eef472ef0f5a9da1bee17060d55b93f5be` | 一致 |
| `integration_11_01_object_binding.py` | 26233 | `606f83b99017140304b1af316e47e1424f32118c22ffe94ca9805982821125f0` | 一致 |
| `stage_board_b_release_11_01.py` | 23016 | `96775815e03743d816676ca30124b36fecbed1b39f8a6a2c3c23b1acfc8549ab` | 一致 |
| `stage_board_b_release_11_01.sh` | 16775 | `143601c5c84a05471b16ea29122a24200fc38e81cf2d4111ed6aca4a6d07bcb1` | 一致 |
| `archive_integration_11_01.py` | 18122 | `59abbb32e69a1e9962ee14f585aa0aa3f378b81bace76c736901ad316554c2f6` | 一致 |
| `verify_and_save_integration_11_01.py` | 21496 | `873457473eb2611e14a1d57efa6652bf049a509d8a16a45d8a0edae75dda9c36` | 一致 |
| `finish_integration_11_01_archive.py` | 7464 | `04d921e65a9d5066950e67215b6c724cd082ea2ab15f9879d08e3f418784f2ef` | 一致 |

候选配置 `integration_11_01_tools_config.json` 末核 SHA 为 `e9298635adc2d85f00165b14d3c6027c759082e82870d4831ceeff70bce2064a`；`basis_inventory.json` 为 `6c13a771d010b14f1a67e1b065ddb44e3687f18f406c14c90df1b840e4951199`。已读 README、操作约定、基线清单及实际配置。候选实填的9工具、2证据、基线包/表和配置指纹清单共14项记录，摘要、字节数、普通单链接文件属性均匹配。

本批不只是改名：在统一命名之外，确有新增运行/配置保护，范围如下。

- `build_integration_11_01.sh`、`verify_integration_11_01_inputs.py`、`integration_11_01_object_binding.py`、`archive_integration_11_01.py` 对应旧批4个功能文件，经过统一 `integration_next_02 → integration_11_01`、文件及格式/标记名称映射后，文本完全相同，没有额外控制规则变化。
- support、stage Python、stage shell、收证及最后收尾5个功能文件额外增加：固定七份配置的前后指纹、固定当前C7程序及原主控程序SHA、运行保护完整字段顺序，以及这些原文在暂存、证据收集和收尾之间的互核。下文已定向审该新增层。
- 未找到指向旧批 WORK、旧输出、旧上传/解包命名空间的代码残留。旧 `integration_next_02` 只在候选元数据的真实C7基线归档路径中出现，属于输入来源，不能改成不存在的新批归档。

## 新批目录、构建和对象来源

support第12–19行及 build第7–12行正确解析工程根，WORK固定为同级 `integration_11_01`；新恢复包与固定证据分别写到根目录下本批的独立 `integration_11_01` 目录。stage Python第202行及 shell第192行仅使用远端 `releases/integration_11_01/<真实release_id>`，上传和解包临时名称也改为本批唯一名称。末核时新WORK、固定证据、恢复包目录都不存在。

build第13–17行拒绝root账户和任何已有WORK，在调用冻结预检之前不创建WORK。第64行取消 `SN100_ARM64_OBJECT_REUSE_MANIFEST` 与 `SN100_ARM64_FROZEN_SNAPSHOT`；实际编译目录为新WORK的 `arm64_build`，没有复用旧对象。实际调用正式ARM64 build.sh，随后只做静态依赖核查、打包和实际产物复核。退出陷阱保留真实返回值，等待tee排空；缺少完整收尾标志不能被改写为0。中断导致缺少退出或终态时，后续工具的完整构建门槛不成立。

对象绑定实现与旧批已审规则相同。独立从AST读取绑定工具保存的规则常量，与当前正式 `shell/build.sh` 去除输入数组后的骨架逐字核对，生成、编译、链接规则完全一致。新 `reagents/original_reagent_probe.h` 在实际headers数组与 `reagents.pri` 各登记一次，没有 `Q_OBJECT`，属于普通头输入，不会凭空新增moc或对象。新的列表追加形式在既有数组解析规则内；计数取自冻结源和实际构建列表，没有硬编码674、687、688、六片或旧对象数。

输入工具以实际五类输入逐项核冻结源码摘要，绑定真实程序、依赖报告、程序包和对象/生成文件。support第352–403行在后续阶段重新读取真实0退出、完整收尾标志、程序和包SHA、ARM64对象及对象来源绑定。这里的“对象来源”仍是按冻结构建规则重建的直接来源，不冒充编译器完整依赖跟踪，也不表示业务已运行。

## 板端仅存包及原运行保护

stage Python第173–176行先核冻结配置及 `allow_stage`，完整构建与包核验成立后才有网络动作；第47–65、223–227行固定板B专用known_hosts、公钥类型、SSH别名及已核主机指纹。上传脚本和stage shell均先读硬件序列号 `8314b1e8ffd0321f`，没有凭IP识别板卡。

上传和解包只持 `immutable_store.lock`，不使用生命周期锁。远端只排它新建本批上传/解包目录，使用不可覆盖提交；已有版本或残留现场会停止，失败不自动清理或重跑。成功只删除本次确切UUID上传文件、临时依赖文件和已空解包父目录。未写current，未发送kill、stop、start、systemctl、产品命令或主控业务命令。

stage shell第134–182、185–249行在持存储锁的解包/提交前后，实际读取并比较boot_id、C7 current、主程序/主控唯一PID、启动计数、执行文件、有效UID、程序SHA、5000监听归属和三份资料路径文件摘要。进程stat的数组索引19对应原字段22；UID取实际有效UID，主程序与当场 `id -u lckfb` 核对，没有把旧PID或UID写成新观察值。快照原文前后必须完全相等，Python和support均按完整字段与顺序复核。

当前C7程序SHA固定为 `50d3f3b510772e717e456cc4e3e910060a82a6635b9d914f033e79f9ed6f0258`，原主控SHA为 `ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff`，与既有keep_running证据对应；新观察仍由当次现场读取。包内 `rk_board.sh` 仅被source后调用 `verify_release`；该文件只有直接执行时才调用main，未因source启动产品。ldd用于实际动态库核对，没有运行SN100程序。

这份保护只证明本次持存储锁解包/提交的前后身份与指纹保持，不是界面、连接业务、资源或长时稳定性测量。外部旧写者不受新锁自动约束，Root仍须按现有分工串行调度；该限制没有被隐藏为工具能保证全部运行时段不变。

## 新增七配置保护

support第126–203行拒绝非ASCII/CR/NUL、多余空行、错路径、缺项、重项或非规范SHA。配置清单必须为Root给定七个路径和顺序的LF原文，且作为完整record进入evidence。

实际固定清单为 `04_references/sn100_host/board_b/20261103_integration_10_keep_running/observation/configuration.sha256`，775字节、7行，SHA `ad11ae68d8ed119a5a34ed65acf98dc2d15bafba6afaa950bee5fd7d3805981c`。只传路径和预期摘要，不复制配置内容，不把清单文本source或eval。

stage shell第11–39行的8项参数与Python第308–311行一致；Base64解码后重编码校验保留规范LF原文。第123–131行对七个普通、非链接实际文件计算SHA，前后表均须等冻结原文；运行保护新增 `CONFIGURATION_SHA256` 绑定同一清单。23个运行保护字段的打印顺序与Python常量一致，没有f-string或bash花括号误展开。

Python保存板端单次输出的配置原文；support核原文、Base64、SHA、单次标记和前后相等关系。收证工具另复制冻结清单及两份板端实捕表，并在收尾前再次核日志、实物及暂存终态；最后收尾同样复核。没有只写一个 `unchanged=true` 代替实际原文。

## 来源元数据和冻结要求

候选 `frozen=false`，两项permission严格只有 `allow_full_build=false`、`allow_stage=false`；最终review、源码表、progress、来源汇总脚本与格式字段仍空。模板字段未冒充终态，任何阶段会先在冻结入口拒绝。

基线包 SHA `56fbff3871eec9776205487ef59d1c9826b6605ec4f8f884bc90b6ceede6b4bf` 与原表 SHA `fd50fedc204888ba6277029715caa80054bd3c760d2c80e1e9c9b93762abb4fc` 正确。fd50与f741内容字典相同、原文摘要不同，support第331–340行要求progress基线SHA等cfg固定原件，不会仅按字典相等混用。

只读见Root实际来源账已生成：
- `root_source_ledger_final_11_01/final_review.json` SHA `1c71afdec3568e547ace6193c533e68d499f132e890aa0817d54500729251abb`；
- 同目录 `promotion_progress.json` SHA `8470f2abccda6a475ee7f6e7e31caaa58a5c299f99990815534d42cea3252ed8`。

两者均688源、`aggregation_only=true`、`executed_git_apply=false`；progress真实complete/phase与fd50基线正确。工具支持按cfg固定实际格式与动态两片列表，未要求旧批六片。将这些结果交cfg不等于本报告执行了ledger，也不把汇总脚本说成重新应用补丁。

Root可按以下条件冻结：
1. 将9份代码按本表SHA复制到独立冻结目录，cfg工具record改指向该目录实际文件，重新固定SHA和字节数；复制后有任何差异须另审。
2. cfg填实际1c71 review、8470 progress、b078最终源码表、v2来源汇总脚本及真实格式；将全部必要来源、失败、批准/Tool事实、Root独审和配置清单纳显式证据，不能把候选空字段或预期688直接置为成功。
3. 保持expected_current为C7及原主控路径/程序SHA和七配置指纹；两permission按Root已授权范围分别赋值，再冻结cfg。没有切版/运行许可混入这两个字段。
4. 固定根属性原件不变。读时根 `.gitattributes` SHA仍为 `c46d8032dc7311b5911dfb7e578ad70ba1258ac924b72675f8b2192c5b9d445a`，本审查未编辑任何属性。

实际完成7份Python AST解析；两份shell及从AST提取的上传文本分别只用 `bash -n` 解析，退出均0（WSL启动器有既有localhost/NAT提示）。没有import候选、执行预检/文件阶段、生成未来产物、改旧批原件、连接板卡或运行产品。

本报告只支持Root冻结本批文件工具。完整ARM64编译、板端另存、恢复包回读和固定证据收尾仍须实际执行并分别收真实退出；新版本运行、真实仪器、界面和资源验证不在本批工具静态通过范围。

