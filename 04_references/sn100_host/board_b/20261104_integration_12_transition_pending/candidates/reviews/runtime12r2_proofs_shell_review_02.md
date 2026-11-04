# 新02运行凭据采集器修订02：定向复核

2026-11-04。对01完整源码审查之后，只读实际01→02差异及必要上下文。结论：01指出的两项缺口已在本实物关闭，新增严格现场摘要解析未见确定阻点。01脚本和失败范围报告原样保留；这不是取证已执行或七阶段已完成的结论。

- **新版未退出封口。**new_run由已核launch回执精确引用。新增check_new_run_files要求规范非链接目录，拒绝exit_code/result.txt任一普通存在或链接存在；每次check_context的入口和出口都核，并在前后各调用check_live，tar前后仍各一次check_context。只有这些实际Shell比较成功才输出null/同活体摘要，没有新增信号、stop/start或锁。
- **准确旧C7九原件。**read_old_stop从activate授权读取精确九键及实际SHA/bytes，逐项核stop回执对应九SHA。result正文精确0/clean1，应用退出与stop工具退出为LF0；旧PID/start/exe/boot/helper/清单值归core652中的C7。activate、launch、observe必须同九records及本链stop receipt/log/exit/意图，回执旧停SHA也同源，不借3446链或旧b31。
- **白名单没有重复。**原52项只新增旧run六份，静态合58；已有meta stop stdout/stderr/actual_exit只补expected原文，不再add。同一新旧run须不同；旧远端路径由固定core/meta构造，不用本地records路径作为tar路径。回收后九件远端字节与本地已核原件精确比较，旧run仍受普通/单链接/规范路径与逐份上限检查。
- **现场摘要严格且保原文。**唯一BEGIN/END及完整尾；单值键集合严格匹配，未知/重复/遗漏/不符拒绝，UTC既核格式又由datetime校验真实日期，服务四字段顺序不影响核对；恰两条固定camera/3A身份，七INI清单加三指针恰十SHA，重复/多余/缺失拒绝。期望来自固定核心和原配置清单，成功保存收到的snapshot原字节。它不新增时间同步/稳定时长或资源门槛结论。
- **未退原只读边界。**仍固定helper21805/core652/runtime_02，动态加载仅定义/main守卫为假，SSH专用指纹和前后新进程/current/原主控/新相机守卫、有界白名单tar和独占Host输出不变。没有life锁、启停/重启/业务包/INI写入；活动app.log变化仍可能导致真实tar非0，此时保存失败，不停程序来收证。

两个Python仅AST解析，未import、调用候选函数、生成脚本、运行SSH/产品/解析器/模拟或读取未来停止九原件。累计patch按实际01/02文本以n=2静态重建，逐字节相同；未执行git apply。起止实际指纹如下。

| 实物 | 字节 | 完整SHA-256 |
| --- | ---: | --- |
| collect_runtime_proofs_batch12_02.py | 27533 | `5d2aa85c7762e1070ba9974af791e40c0dcf0d1126964eac50d935e03f19c47e` |
| 保留01 | 19348 | `d08fea2553d74b51aca948d7310daa7a50712b49e6473d75a9a9008fd016453d` |
| 依赖LIS helper | 19245 | `21805f6c036da483cba5a0787daf67a1001fff568a15321967a130e1af6a744c` |
| core_inputs.frozen_01.json | 9604 | `652ee775372fea069aef634d4a80ca703c199e9c310dab839ad196c19f8f6424` |
| 01→02累计delta.patch | 10897 | `f3a7ddb024b9d4b2d431c90f28684d31e84a9500485d59a123fc62fff71b685a` |
| 作者修订说明 | 2713 | `6f9dc6a85406fe1471f1a15a18e45235e13c1bc226da33ff6ec897e1ff1fadaa` |

后续仍须Root先取得完整七阶段真实0/stop9/launch/observe，再独立执行收证及核实际结果；软件被动观察、真实LIS、仪器、实体和资源分开登记，新程序退出仍null且复核后保留运行。
