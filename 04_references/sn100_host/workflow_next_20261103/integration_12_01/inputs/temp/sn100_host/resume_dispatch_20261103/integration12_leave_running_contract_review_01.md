# 第12批仅编译、独立存包的C7留驻边界

2026-11-04。按本轮“急诊恢复定义先列待办，先推进其他事项”的范围，读取第11批冻结cfg、九工具的相关输入/存包/封口逻辑和已完成的stage复核，并核Root下一批Uh六文件组合的预期表。只新增本说明，没有执行phase、产品、SQL、模拟、编译、网络或板端操作，没有改旧工具、正式源码或公共文档，也没有生成运行器。

结论：第11批的23字段身份检查、七行配置保护、同一存储锁内解包/提交前后核对逻辑，可以作为第12批的基础；第11批固定工具和结果不能原样直接当成第12批使用。它们硬绑定了第11批源码、格式、工作目录、远端版本目录、构建/包/日志和归档路径。必须另存新工具候选，绑定第12批真实输入，实际执行后另取第12批guard。旧guard只能作为历史比较依据，不能算新板端观测。

本次实际读取的第11批cfg SHA为 `051977b52473c907e3df8dba3245781432aa619b79cb0a9f4b22b2430b92186c`，其中九份冻结工具实际SHA均匹配清单。第12批工具尚不在本报告的实物审查/接受范围；下面是交原构建窗口的具体核对合同。

## 必须重新绑定的实际输入和命名空间

| 项目 | 第12批所需绑定 |
| --- | --- |
| 最终源码 | Root实际接纳并重算后的完整清单、数量及final_review，不只拿六文件补丁或私有预期表当正式扫描。当前Uh组合预期为688项、表SHA `0e5c70ef9d5ac5fa3db34888103b4158783141aac5f90845e225c720d532ac8d`；它仍须与实际接纳终态相等才能作为构建输入。 |
| 晋级记录 | 第12批实际promotion_progress、promotion_script记录、approved_final_review_sha256、六片组合补丁及相关审查原件。补丁候选SHA `d89e920d59e6fde53cb87a1290dc1a67483fb072fd38321071a9799d3ba71c15` 只能在实际Root记录采用后进入冻结cfg，不借第11批已晋级记录代签。 |
| 源码前像归档 | baseline_snapshot必须等新review.base_source_files，baseline_archive必须与该来源相符。12若从b078/688增量接Uh，须绑定相应已封口的第11批源码快照/归档的真实路径和SHA；不能继续把C7/f741旧快照fd50…当这个新源码前像。运行expected_current仍是C7，两个基线含义不同。 |
| 工具 | 新独立工具目录、九工具实际SHA和cfg；Python import、CONFIG文件、batch/format标记、构建/收集/封口解析条件彼此配套。不可只改shell输出目录而让support/validator仍校验第11批终态。旧冻结九工具和原失败/通过原件不覆盖。 |
| 电脑输出 | WORK、构建目录、source_snapshot、build_inputs、object_binding、依赖报告、包、started/complete、每条命令stdout/stderr和exit都归第12批新目录。保留完整构建的Release/ARM64输入核对和取消继承对象复用环境的边界，不能拿第11批旧二进制代替本次失败构建。 |
| 新包身份 | release_id、SHA256SUMS、主程序/报告辅助程序SHA、包SHA、依赖报告和构建日志必须由本次真实构建产物形成，不能预填第11批356fe…及4f90…包SHA。源码表SHA也不能直接当release_id。 |
| 板端保存目录 | 固定本批独立namespace，例如 `releases/integration_12_01/<真实release_id>`；上传唯一文件、`.workflow_stage_12_01.*`、远端路径和日志标记一起更新。目标已存在或有未结清现场时保留并拒绝覆盖，不拿清理旧temp/释放旧锁来绕过。 |
| 归档和证据 | ARCHIVE、EVIDENCE及provenance/collection/finish记录使用本批独立目录，逐项绑定真实包/源码/工具/命令原件。第11批档案可以作为历史来源引用，但不把其stage/finish0复制成第12批执行结果。 |

其中源码前像有现成的强校验：第11批support第253行要求baseline_snapshot字典等于review.base_source_files，第255行要求final_source_manifest等于review.final_source_files；promotion第324—334行又核前后表、补丁、review/script SHA及该baseline_snapshot SHA。第12批应保留这些核对，按真实新来源填值，不放宽成“文件存在即可”。若第11批归档尚未封口，依赖项保留待实际原件，不能编造新SHA使preflight通过。

## C7与板端存包检查清单

以下均为第12批待执行项；本次阅读第11批结果没有替这些项目打勾。

- [ ] 只开放完整构建和stage能力。禁止新工具分支切current、stop/start、TERM/kill、systemd-run/openvt/runuser产品、重启主控或调用自动退出检查器；不得新写默认30秒运行器。file/readelf/ldd等架构和依赖检查与产品运行结果分开记。
- [ ] 每次网络动作先核板B硬件serial `8314b1e8ffd0321f` 和专用已知ED25519指纹 `SHA256:S4ihaw0V/Z8w7v3VlJOQEFLEH3Hp6nr3KjiUTxanwPg`；保留StrictHostKeyChecking、固定别名及专用known_hosts。新记录绑定该次实际known_hosts SHA，不把第11批网络成功当本次身份核对。
- [ ] 继续使用同一 `/home/rkshare/programs/sn100_shell/immutable_store.lock` 排他锁。不能改成每批各自一把锁而失去跨批串行保护；file-only存包不取得应用启停/显示控制权，也不为存包等待或抢占现C7的生命周期锁。
- [ ] expected_current保留C7完整路径 `releases/c7cbf438a3a034b25ae22accfaf48bacf0717174ce85bef2203ce8db143cb06e`；原应用SHA仍为 `50d3f3b510772e717e456cc4e3e910060a82a6635b9d914f033e79f9ed6f0258`，原主控路径 `/userdata/shared/ACA_V1.0OffLineD-500`、SHA `ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff`。不能把待存的新release写成expected_current。
- [ ] 从实际板卡重新取本批before和after的23字段快照，分别保留BASE64原文、SHA和文件。除本批前后逐字节比较外，总控还核对原C7 `669382/2102645`、原主控 `5008/118180`、同一boot、实际UID、exe路径/实际SHA、5000监听和三入口文件，不能只抄旧PID或只凭PID认同一进程。发生外部重启/身份变化时保留失败，由Root先查事实及实际生效INI，不静默改保护值或补启程序。
- [ ] 七配置保护仍可引用原 `configuration.sha256`，SHA `ad11ae68d8ed119a5a34ed65acf98dc2d15bafba6afaa950bee5fd7d3805981c`、775字节；但本批须实际重新计算七份文件的指纹，解包前/提交后原文都等冻结原件，三入口文件也须重新核普通文件/真实路径及SHA。不能把第11批两个配置guard复制进新目录作观测。
- [ ] 在持同一存储锁的解包前和另存提交后分别取guard，包和版本目录做真实哈希核对并拒绝覆盖。标明guard_scope仍仅 `locked_unpack_and_commit`，不能把它扩展为整个编译/上传/归档期间连续采样或长期留驻证明。
- [ ] 新stage_started在网络动作前独占保存；真实upload/stage退出原件均0、所需标记唯一、所有本批日志/guard/输入SHA相符，才写stage_complete。activation_performed=false、program_started=false以及ACTIVATED=0/PROGRAM_STARTED=0须与实际操作一致；不预填未来0，不新增“新应用退出0”。

23字段v2的身份检查算法可以复用，批次format和writer/parser必须配套，不能让第11批的固定format/路径验证误认新结果。七配置和C7期待值的继续使用是保护条件复用，不是重新观测已经完成。实际INI变化、同板进程身份不明或板卡指纹不符都保留现场，不能通过启动第二个主程序、重启原主控、覆盖INI或套参考工程配置来完成存包。

## 后续启用的边界

本批file-only结束只说明新包和证据已保存。编译进新包的Uh消费者/被动观察代码不会因此在现C7上生效，不能给正在运行的C7另注入观察ID、改环境或启动额外waiter来扩展本批范围。后续实际启用仍由Root按当次任务串行绑定新release、实际旧程序停止事实、真实current变化及新启动身份；这些运行事实不得由本批文件校验预填。

届时需要保持原配置/资料/原主控，先核板卡及实际INI，排除重复GUI后只启动一次；长期留屏使用明确不限时入口，不能借旧30秒READY超时路径杀程序。新产品运行、真人Uh输入和真实TX、独立Uh批次保存/重读/页面采用、实体与资源结果分别留证；没有真实条件继续待验，不造包或模拟代替。

急诊恢复定义及可信会话/无旧占用来源仍列待办，不因本批编译存包启用UI或伪造准入来源。本报告没有给第12批工具、构建、板端存包或新功能打通过结论；待原窗口交实际工具和Root冻结实物后，再按本清单核对。
