# LIS A第13片：公共构建和源码恢复范围独审

2026-11-04。只读`workflow_next_20261103/root_lis_a_candidate_13_01/`实际12片、补丁、私有回演记录和必要正式前像；结论：未发现确定阻点。本次不重审九源业务，不运行候选snapshot/生成器、产品、编译、SQL或板卡，不改正式/候选。

- 九新增文件逐字等已双审A9（candidate_manifest df2424…2501）及私有回演后像；候选恰12项，补丁也恰12同名路径，3改9增无额外入口。正式三公共文件当前字节等其base，其他正式688全表状态依据Root本轮冻结，不冒称本Agent新扫全树。
- shell/shell.pri只登记original_lis_query、download_codec、application_plan、download_ack四个cpp和对应五个h；实际qmake入口shell/shell.pro仍include该pri。shell/build.sh只在实际输入枚举之前追加同四cpp五h，路径/大小写与九源完全相符，未改任何flag/链接/主入口。新头无Q_OBJECT，不增加MOC；已有-Iprogram_dir可解析lis_transport/lis_connection_values.h，相邻include可解析本目录。
- package_arm64.py唯一源码差异是在snapshot目录元组加入lis_business；新九源因此同shell构建文件进入源指纹和恢复源码包。未改打包执行/部署/能力开关，不误接raw inbox、main、worker、SQL或生产QRY。纯函数链接不证明已由真实链调用，Prepared也不是保存/发送许可。
- 原混合行尾按局部编辑保留：shell.pri新增十行CRLF，既有9行单LF未被规范化；build.sh保持LF；package_arm64.py保持CRLF。没有全文件换行或空白清理。
- 12实际后像SHA均匹配changes_12及private_replay对应文件；before688→after697仅这12差异，after字典等virtual。Windows native snapshot仅将反斜杠映射为正斜杠，无碰撞、不改SHA，规范表697项SHA为ed4a…。这是Root私有snapshot已执行记录的只读核对，不是正式接纳或本Agent重跑。原命令check/apply/snapshot实际退出文件均0。

| 固定对象 | 字节 | 完整SHA-256 |
| --- | ---: | --- |
| 普通补丁lis_application_plan_A9_on_0e5c.patch | 52043 | `f0050002c3355ea80c2e8845fb9515443f641439bf549bee872ab6249f007d5a` |
| 三公共后像shell.pri | 8583 | `53dac852a16f7416089d416276519b7fd1d57cad3a6c6f57785fe85f85c0c320` |
| 三公共后像build.sh | 29437 | `947d8c4a0db488344ece26bc157dd34655c866b67cb2be08077fbe5691152c5a` |
| 三公共后像package_arm64.py | 16863 | `7fff3c900c5ef6e62618e9ac323bd8b9764b30ad7661cb542b9ad21a2c45ffd1` |
| private_after_source_manifest.json | 74988 | `ed4a3eb3025ef0602cdb49d41a6094f35011eb68ac19cf9f36e4f75fb3f6e254` |
| changes_12.json | 4507 | `88dc03bc06ac9fd9ba2bf1e17cb43b3d226363a11754512c759c76448e6dec5b` |

package Python只AST解析0。后续由Root串行正式接纳、重新实际枚举并完整ARM64构建/恢复包回读；纯A未接生产调用，不能用程序启动代替真实患者/任务应用或ACK验证。C7关闭待处理/未启新版的现场状态不受本片源码准备改变；真实LIS/仪器/实体/资源仍待各自条件。
