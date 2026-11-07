# 医院资料删除候选：只读审查

2026-11-05，按总控追加范围，仅审 `resume_hospital_draft_20261105_01/hospital_remove_preserve_draft.patch`、该目录before/after的两份页面源码，以及必要 `HospitalStore::remove/checkReady/save` 接口及实现。未改医院候选或正式源码，未编译、运行产品、联板或另派任务。ASCII候选保持冻结，本报告单独追加，不回写原交付清单。

**结论：未发现阻止本片采用的确定新增错误。**成功回执只移除已确认删除的目标行，保留医院名和其他草稿的主路径成立；下列结论均限源码，不是Windows或RK运行通过。

## 逐项核对

- **匹配回执及真实提交：**after CPP 140–179要求busy、remove操作、目标ID、待删类别、当前类别和已显示类别全部匹配。Store CPP 730–779按type/id删除，要求恰好一行并提交成功后才发 `removed(type,id)`；检查失败、SQL失败或提交失败发 `error("remove",...)`，不发成功回执。当前页面一次只允许一个待处理操作，未见本片绕过真实提交。
- **只删目标行，保留其他输入：**成功时按隐藏ID查找并 `removeRow`，同步从已存快照 `displayedRows_` 去掉该ID；不调用 `renderRows` 或重新加载。因此医院名文本、其他行控件及其文本，包括未填完的新行，都不被旧快照覆盖。剩余行的名称/助记符信号绑定捕获控件指针而非固定行号，编辑器事件过滤也动态取当前索引，未见删除后把编辑写到旧行位置的新问题。
- **脏状态：**`updateDirtyAfterRemoval` 比较当前医院名、剩余行数/ID/三个字段与删后已存快照；不调用保存校验。其他草稿/新增行仍存在则保持脏；仅目标行有修改且它已删除、其余完全相同则可变干净。删除的是已提交记录，不应再把它作为待保存草稿恢复。后续“放弃”用删后快照恢复，已提交的删除不会被撤销。
- **删除失败及恢复：**匹配remove错误清掉待删ID/类别、解除busy并发失败结果，不删除页面行、不重绘医院名或其余输入，不改dirty。下一次删除/保存会重新登记操作；取消/刷新沿原确认和加载处理，不会被残留的待删ID挡住。Store失败路径不返回loaded，因此本片屏蔽删除期间loaded不会把正常删除错误卡住。
- **确认框取消及未保存的新行：**确认框选取消直接返回，没有登记删除或请求Store。ID≤0的新行仍只从页面移除，不请求数据库删除。该分支的 `setDirty()` 是before就有的行为：若“原页面干净→只新增一行→再删掉该新行”，dirty仍为true，可能多一次放弃提示。这是确定的既有保守标记，本片未修，但不是本片新增的数据丢失或阻止后续保存的问题；不据本次审查声称所有脏状态场景均已与原版一致。
- **后续保存：**删后剩余行仍由 `collectRows` 按当前ID和控件值采集，医院名取当前输入；点击保存重新置 `pendingOperation_="save"`。删除成功或失败都已清零待删ID，故saved的新守卫不会拒绝这次正常保存。保存成功仍接受已提交ID/快照再按原流程读取核对；保存/校验失败仍保留输入。新行名称为空时，删除本身不受牵连，真正保存仍按既有必填校验拒绝，没有为了保留草稿放宽规则。

实际RK仍需核：修改医院名和另一行后删除已存行，其他草稿保留且可继续保存；目标行有修改时只丢目标行；未填完的新行不阻删除已存行；取消删除、真实失败后的保留与重试；删除后放弃其他草稿不得恢复已删记录。数据库写入结果、输入焦点/选择和正常退出另取实际证据，本报告不执行这些步骤。

## 输入SHA-256

下列候选路径均相对 `temp/sn100_host/resume_hospital_draft_20261105_01/`，Store路径相对仓库根。审查以这些字节为准，不复核旧697源基线。

| 输入 | SHA-256 |
| --- | --- |
| `hospital_remove_preserve_draft.patch` | `ce8affa000d829902160bddb66691155dcd41837c1015ddf47ba2d6af8c01ed8` |
| `before/06_programs/sn100_host/ui/frmhospinfosetting.cpp` | `f800e9e64ed69f3f5f20419c00b4e09a6104df79e4e7508ec31a890938e99c59` |
| `before/06_programs/sn100_host/ui/frmhospinfosetting.h` | `6c24d118e520cf5810ce03a182a4894b41ad77cff202f60f9ab1646014155b44` |
| `after/06_programs/sn100_host/ui/frmhospinfosetting.cpp` | `2947110bd2f09c1227f14df205f044dc37169bbea8883fc28a481f26ff1186a4` |
| `after/06_programs/sn100_host/ui/frmhospinfosetting.h` | `7907a28a629ccb3a3d069c6c6ad0632e27f2ecd458ce0d4b2de7ea52b218daf2` |
| `06_programs/sn100_host/data/hospital_store.cpp` | `7e90219f924f30d59253459ebc30543a46c45ff7e1267e9c53611fa3050977b0` |
| `06_programs/sn100_host/data/hospital_store.h` | `ff20b4eab9b91296cf7c52b4d5ba0cfe940cef2822da4f7462d89910f66085c2` |
