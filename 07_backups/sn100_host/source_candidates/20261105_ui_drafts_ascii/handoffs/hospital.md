# 医院设置：删除一行保留其他草稿

本片已准备完成，供总控独立审查，尚未采用、编译或上板。目录resume_hospital_draft_20261105_01；只改ui/frmhospinfosetting.cpp和.h，普通窄补丁hospital_remove_preserve_draft.patch、前后SHA见file_hashes.json。无data/Store依赖修改，不动权限、保存、取消/切页确认和公共离页逻辑。

## 本次用哪份源码

以07_backups/sn100_host/board_a/resume_20261104_02/source_snapshot.json为准，本轮开始逐项实读697份全部相同，0差异；该清单SHA为d24ee5f9ab0786ff15528c3958f997e47181caab9fbfe5373abc36f2833d218d，副本和实际核对结果分别是base_source_snapshot.json、base_verification.json。不是021d原697前像。11月4日已采用6源和未提交文档/归档均保留，本窗口只写本候选目录。

## 原操作和确定问题

原版frmuserset/frmhospinfosetting.cpp的on_btnDel_clicked（455行起）：先确认；已有ID调用Delete，失败提示并return；成功只removeRow；未保存新行不写数据库。医院名称和其他行控件原样保留。依据还包括settings_navigation.md的S02，文件SHA见evidence_index.json。以上只做只读源码对照，没有运行Windows产品。

当前移植版removed回调从displayedRows_重建整页，再requestLoad；renderRows同时把医院名称、表格值和dirty_恢复成已存值。因此删B会抹掉医院名、A行及未填完新行的输入。这是明确的源码缺陷，不依赖本次人工登录才能准备修复。

## 候选怎样改变结果

删除成功只按后台确认的类别/ID移除界面B行和displayedRows_中的B。其他控件不重建，displayedHospitalName_和其他已存行不变；不再发删除后的requestLoad。与更新后的快照比较医院名、行数、ID和三列输入，重新判断是否仍有草稿；不用保存校验读取，空白新行也保留。只有B本身曾修改、删后已无其他草稿时，不留假的未保存标记。

页面在发删除请求前记住类别/ID；成功回调须同时匹配busy、remove、类别、当前显示类别和ID，完成或失败后清标记。不匹配或已经完成的removed回复不能再删其他行、重置忙态或恢复旧数据。loaded在删除期间/空闲时不接旧回包；saved也只接当前保存阶段。删除期间其他操作的错误不提前结束本次删除，迟到的remove错误不结束新的保存/加载。Store现有接口没有请求序号，本片只在既有单工作线程、页面一次一项操作的条件下核对这些门槛，不声称已建立跨所有外部写入者的通用请求协议。

删除失败仍走真实Store error，保留界面、已存快照和草稿，不假删/假成功。数据库本来只在事务提交成功后emit removed，未更改。删除确认中“其他草稿将放弃”的过时文字改为会保留，未增加确认步骤。保存仍按原现有collectRows、原子写入和提交后核对执行；保存原子性、切类别/取消额外确认的其他原版差异均不在本片修。

## 已核与待验

补丁适用性/空白检查实际退出0，前后文件字节及源码边界检查见static_checks.json。未编译、联板、登录、启停、切版、发包、运行Windows/WSL产品、做脱机/模拟测试、提交推送或删除文件。资源、实屏、实体输入和实际失败恢复均待RK条件。

最小验收见board_a_verification.md。只有原资料里获准可删的字典行才做删除验证，不能借验收删除未获准的现场业务数据。暂无人工登录/失败条件时登记待验；本片代码准备可独立完成。后续若总控采用，先核两份before SHA及当前依赖；不要把before或base清单整树覆盖其他窗口成果。回退只反向本窄补丁且须先核after SHA，没有重置数据库的步骤。
