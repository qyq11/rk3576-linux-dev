# 单孔Mg修订04：交总窗口窄补审

修订04只修单孔探测的选针输入。旧freeze03被总窗口业务独审发现确定的左针误判和遍历差异，不能发送或作为当前放行版本；旧源码、补丁和报告均保持原件。本片已完成作者修订、两名独立窄复审和临时目录补丁应用检查，**仍待总窗口补审与整合，不是编译或设备功能验收**。

## 改了哪里、原版怎样做

新目录 `temp/sn100_host/reagent_single_probe_20261103_04/`保存独立稀疏候选。相对旧freeze03只改 `candidate/06_programs/sn100_host/reagents/original_reagent_editor.cpp`：新增私有 `singleProbeNeedles:160–206`，`prepareSingleProbe:734`改为一次调用。其他12份源逐字节保持03；普通ProjectRepo、任务缓存、剩余次数、旧generic探测、N/页面/通信和来源租约接口没有改动。

原Windows `frmsystemmaterial.cpp::loadAllProject:689–742`排除文件名包含小写formula的项目，再按项目xuhao和原sendSequence前缀排序。`loadProject:2120–2126`经 `SqlBase::getData:774–805`实际用无ORDER BY的SELECT *读取project_reagent，原针字符串精确为`"1"`转左，其他转右；`frmreagentmaterial.cpp::dectLV:801–810`按项目、实际返回步骤顺序让同名试剂后者覆盖，最终左为1、右为2。旧候选拿未经翻译的原串直接比较中文，并使用已按xuhao,rowid重排的普通缓存，还遗漏formula消费过滤。

新助手在现有Store后台两把合作写锁内，使用已有只读连接和原SELECT *，不附WHERE、ORDER BY、rowid列或字段投影，按query.next逐行覆盖。精确`toString()=="1"`取1，其他取2；仅从本Mg消费中跳过小写formula文件，不按enabled过滤。无匹配仍由原编码器用区域后备针，不改Mg八字段、编码、单位、精度或本地token的处理。原版精确路径、指纹和差异详见 `scope_and_evidence_04.md`、`implementation_04.md`。

每项目4096行现有上限、准备实际期限、读错误与lastError检查均保留或在新助手明确落实；超限/缺准确字段/失败/过期拒绝整次准备，不截断、不发布半份映射。原SQL失败时继续的弱保护不是本片成功域。SourceLease仍核所有项目、formula及三库/ISI的完整版本和目录集合；只是消费过滤，没有删掉来源。Root发送前撤销和期限门继续生效，不能声称每行即时中断SQL。

## 固定版本和两份补丁

- 最终13源清单：`freeze_04.json`，SHA-256 `f1f21c931da5d4b1b2cbfbf0c02dbc8bc6c576647ef9c16bee1f2148c5efead7`。
- 唯一新CPP：`bb68c3c1d21f66fcb31483d9dc6b4bfcdfbf003e019459f0872ed08ede364163`，CRLF、UTF-8无BOM；03前像为`2490d6c3262cc740d4425d49513d96104483ea677c31a63e05c3ae6802d50258`。
- **给本次窄复核的03→04补丁**：`delta_03_to_04/single_probe_03_to_04.patch`，SHA-256 `b72fd703ab4ced815641f3f302069a2f69dc886ec6a9c9cadf6568d8bc4850f6`。仅1个CPP、2个改动块，50行增加、4行删除；不能直接当相对正式树的补丁。
- **累计b688→04补丁**：`packaging_04/single_probe_on_b688.patch`，SHA-256 `3b5a0f5d249ee621da310f1f2e32f4dff794e8459b1f9c0df8da1245b09b1a0c`。12个既有源和1个新增纯值头，完整路径/字节在freeze04；基线仍是原687份b688表，本任务只复制12份确切前像，没有复制整树。

两份补丁均在各自私有temp完成diff空白检查、apply预检和实际应用，退出均0；单源增量得到同一新CPP，累计13份得到同一freeze04字节，见两个目录的 `commands.json`、`acceptance.json`。没有提交或推送Git，没有对正式树应用。文件补丁检查没有运行产品、执行SQL或模拟报文。

## 独立审查和原件状态

总窗口03业务报告 `reagent_probe_final_business_review_01.md`（SHA `666b05e8ccf301e2f5738d16da37c2058e5594c478c9117f1e2ea00f1d0216a2`）的P1/P2是本次逐项修点；其异步报告SHA为`7c64367199ac36e211f5a066cf31140036db278eb25736eb11c24b18fa6ad156`。旧作者报告不替代总窗口后来发现的原值链，也不回写为旧03通过。

新04两名独立审查者各自直接回读了必要源码，末核13/13等于freeze04，其他12份等于03：

- `reviews/original_value_and_async_review_04.md`，SHA-256 `c814142dbde70a5c7c34d7e422950c6b5d5bfa57be67befe1f03999652411260`：原针字符串、原步骤查询和小写formula消费过滤均有对应修正，失败半结果/期限/上限及原异步边界未见新确定阻断。
- `reviews/source_admission_review_04.md`，SHA-256 `bdbd686a4870c767161d56313b72856fc601f4a8f3e60ecc5a038fe340013765`：所有项目含formula仍完整核对，来源保留至send返回的合作写锁合同没有缩小。

这两份是独立源码窄审，最终还需总窗口对04补审，不称运行或全部合法输入验收。SourceLease只约束合作写者至本次send返回；无ORDER BY查询不保证跨SQLite实际物理返回序，现场是否有重复步骤/覆盖差异本轮未查库。

## 整合与未验证项

累计候选仍基于b688；当前正式含B1的两份主窗文件已经不同。总窗口须以三侧真实前像窄合，保留B1手动请求、唯一开始出口及User准备链，不能整份覆盖正式文件；新增 `reagents/original_reagent_probe.h`仍需总窗口登记构建输入。本片没有新增公共API、构建项或源文件；是累计候选中原有的新头，不能把它漏掉。

当前687/f741构建批不含Mg。本窗口没有修改正式/main/build/pri/公共计划记录，没有构建、运行Windows/WSL/RK产品、执行新增SQL、读取业务库、模拟或联板。原版核对只用只读源码/既有资料；不安排Windows产品运行。实际原资料的重复步骤/跨项目覆盖和最终Mg针号、完整ARM64构建、获准RK/真实仪器中的Mg/N自然先后、真实提交/页面采用/读者返回、取消断线/关闭及资源/同板影响均仍待总窗口安排。无请求号的迟到归属、跨进程未知恢复、整盘/扫码/None或Unknown等未完成范围保持，不借本片放宽。

最终指纹与旧03原件保持核对见 `delivery_manifest_04.json`。作者已停写候选，等待总窗口补审结论。
