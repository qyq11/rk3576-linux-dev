# 单孔Mg修订04：选针与原遍历窄补审

2026-11-03，独立审查者`/root/review_amount`。仅只读03→04的真实单CPP差异、新增读取所用Database实现及必要调用上下文；未重扫正式687份源码，未重读其余12份候选源，未执行SQL、构建、产品、模拟、网络或板卡操作。仅新增本报告，03版不放行报告与候选原件均保留。

**结论：本次所审专用合法配置单孔纯Mg路径的P1针号转换、P2步骤读取顺序和formula消费过滤，已见对应源码修正；新增读取和发布边界未发现新的确定阻点。** 这是针对04固定字节的源码补审，不是整片重审，也不表示ARM64编译、实际SQLite返回顺序、RK、仪器或资源通过；当前已编译c7包不含Mg，不能把该包结果转给04。

本次绑定，路径相对`temp/sn100_host/reagent_single_probe_20261103_04/`：

| 对象 | SHA-256 |
|---|---|
| `candidate/06_programs/sn100_host/reagents/original_reagent_editor.cpp` | `bb68c3c1d21f66fcb31483d9dc6b4bfcdfbf003e019459f0872ed08ede364163` |
| `freeze_04.json` | `f1f21c931da5d4b1b2cbfbf0c02dbc8bc6c576647ef9c16bee1f2148c5efead7` |
| `delta_03_to_04/single_probe_03_to_04.patch` | `b72fd703ab4ced815641f3f302069a2f69dc886ec6a9c9cadf6568d8bc4850f6` |

本人直接比较03、04的Editor文本，实际只有两块：第160–207行新增`singleProbeNeedles`；`prepareSingleProbe`第734行替换旧缓存中文比较循环。两份13项冻结表仅该CPP的SHA不同；这里只读元数据作范围核对，未以它代替其余12份再次审查。原03报告`reagent_probe_final_business_review_01.md`的SHA仍为`666b05e8ccf301e2f5738d16da37c2058e5594c478c9117f1e2ea00f1d0216a2`。

1. 原针号转换已经接到真实读取值。

   新函数第196–198行对同名试剂的原数据库值执行精确字符串`"1"→1`、其余`→2`，每次匹配覆盖局部map。它对应原Windows `frmsystem/frmsystemmaterial.cpp:2120–2127`先将原串1转换为左针、其余右针，再由`frmreagentmaterial.cpp:803–809`编码1/2的两步实际行为。新纯Mg不再用未经翻译的数据库原值比较中文。没有匹配时发布空map，仍由现有Mg编码器采用原区域后备针，没有新增默认覆盖。

2. 原输入遍历差异已经在Mg局部修正。

   第179行实际查询`SELECT * FROM project_reagent`，未加xuhao、rowid或其他排序，也未再使用已按xuhao重排的`advanced.reagents`。与原`SqlBase::getData`无sqlWhere时的查询规则相符；第170、191、198行保留外层有序项目遍历、内层真实query返回顺序及后匹配者覆盖。第174行按`originalFileName.contains("formula", Qt::CaseSensitive)`跳过消费，与原Windows文件名小写formula过滤一致。

   外层`orderedOriginalProjects`和完整`probeProjectFilesMatch`未变。过滤只影响哪些项目参与针号覆盖，没有缩掉原缓存/版本/目录完整集合证明，未改全局任务缓存排序。SQL规则相同不等于已验证跨SQLite版本的真实返回结果；实际原资料的读取顺序与完整Mg仍须后续RK取证，本次没有开库或用造数据替代。

3. 新增错误、读取连接、4096行和期限边界。

   - `Database::open(...,false,...)`沿正式`original_files/original_business_database.cpp:58–77`打开现有工作根内的只读QSQLITE连接，使用独立UUID连接名和200毫秒busy timeout；不创建表、写库或启动复制恢复。调用仍位于既有Store后台来源租约内，未把SQL放到GUI。局部QSqlQuery在Database之后创建，所有退出路径均先销毁query，再由Database析构关闭并移除连接；项目间不复用旧query。
   - 打开、执行、缺少精确字段、读中错误均返回false；第200–201行另检查读完后的`lastError`，不会把中途失败的部分行当成成功。行上限在第194行于第4097行进入前拒绝，4096行完整读取可继续；计数覆盖全部原步骤，不因只保留目标试剂就跳过上限。该上限沿现有项目读取的4096行保护范围，不是新增“截断到4096后成功”。
   - 使用原Request的`prepareUntil`，在每项目、打开后、每行、每项目末及最终发布前核真实单调期限；没有续签10秒预算。阻塞式数据库调用本身未被强行中止，但其返回及后续发布仍会受期限检查，超过期限不发送。它不能作为读取耗时或资源预算已经达标的证据。
   - 针号先只放局部`selected`，所有项目读取、错误和期限检查通过后第205行才整体赋给输出。调用方第734行false立即退出，Prepared在第695行只保留原请求、success默认false；成功准备还须通过第736–737行原Mg编码、完整版本/INI和批号View复核，第739–747行才发布success结果。没有用新读取指纹给旧Prepared续签，也没有把读部分map当成已准备Mg。

只补看必要正式Database实现：头SHA`1b7c9907d7196e1f6c8dc6e3ce0763f2e6e4a63fe631af751feb8ad2ad8105a4`，CPP SHA`4fa8579a98d7e4159a99de14cb4f9bf5d8d069048a22c018807a22b7f474c55b`。没有调用其中函数。

旧`prepareDeviceOperation`分支第934–935行仍有缓存与中文比较，该分支没有在本次改动中修复；当前专用单孔入口走新的`prepareSingleProbe→singleProbeNeedles`，不经过它。本补审只关闭该专用纯Mg路径的原P1/P2，不把旧设备准备、整盘或其他未来消费者标为已通过。原03报告中B1公共重叠、来源锁仅保至send返回、无设备业务请求号、跨进程未知恢复及全部运行待验范围继续保留。

本报告不批准向当前批合入。若Root以后采用04，仍须在当前正式版上串行处理两个B1共享主窗文件、固定组合后像并按实际产物安排完整构建和RK/真实仪器核对；不得用c7或03旧包补足04的验证记录。
