# 提交未确认整盘遮罩：第二版窄复核

2026-11-03。对第二版冻结源码只读核对，原P2已关闭，未发现这一修点引入的确定阻断。这里只确认源码与来源清单，不构成正式晋级批准、编译通过或RK/真实仪器验证结论。

本次只新增本报告，没有修改候选、正式源码、07、旧补丁、旧清单或原问题报告；没有执行补丁、编译、运行产品、打开数据库或联网。作者的`review_fix.md`只作修订背景，本结论来自实际源码与674份文件逐项指纹核对。

## 固定输入与实际差异

第二版源码根为 `C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/task_reagent_root_candidate/revision_02/candidate/06_programs/sn100_host/`。下文短路径均相对于此根。

| 输入 | 实际SHA-256 |
| --- | --- |
| `revision_02/packaging/task_reagent_root_on671_v2.patch` | `3e29c16c008482cc51c51ee890d751fc6989ec8fac63fb2ae64e96f737450e82` |
| `revision_02/packaging/manifest.json` | `ead7054087684e9ffebc7b5abaab41fca51978047d524565510278471a969f19` |
| `revision_02/packaging/fix_c_scope_on674.patch` | `bc404a807268d4ad50d4768785b71fc97ac4028ff5c8ba30826cb4eba83fc83a` |
| `revision_02/source_snapshot.json` | `957d7991629c4eff7103324255e9036252c008cc0f332e92d85daddaebffa906` |
| `revision_02/review_fix.md` | `20ba40e555b25940137aa8bae0b975e8dd5b363fc4ff303e39fa24e89cd13818` |
| 修改后的`shell/task_reagent_shell.cpp` | `e27fc51eb505798a9082a7553506c50d200ec60ec6af375940d7e9b86758b518` |

实际读取结果：

- 旧候选和第二版候选各674份文件，完整相对路径集合相同，没有新增或删除。
- 逐文件回读SHA，只有`shell/task_reagent_shell.cpp`由`ed3d788aff3519ac7ab13c869c6d87a9aa2d4025eef13dad07d1cc635452af10`变为上表e27f版本；其余673份逐字节相同。
- 第二版全部674份实际文件均与957d快照相符。单个修改文件中，`foldTransaction`之前和`foldDisplay`起的后续全部正文精确相同；差片只有该函数内的一处修改。
- 既有原问题报告仍为`2a14bbf28962c92b291b29352f0a3f6e8b95b8ee34bcb1bdfd87d361ca3528d7`，旧累计补丁仍为5cf43ed5、旧清单仍为62996383。没有覆盖原问题或改写原失败结论。

累计补丁的前像是原671底稿；差片的前像是旧674候选。两者为不同起点的替代接法，不能双重应用。本审者没有重新执行check/apply；清单中的作者执行记录与本次静态全文件比较分开看待。

## 原问题为什么已关闭

原问题是把“能继承任务前显示量的孔”误当成“未确认SQL可能影响的孔”。任务前A孔可信、B孔未知时，B没有可信GUI量并不能证明SQL不会涉及B；后来真实Ur若确认B，C原先只遮A便会漏显示B。

第二版[`foldTransaction:75`](C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/task_reagent_root_candidate/revision_02/candidate/06_programs/sn100_host/shell/task_reagent_shell.cpp:75)现在保留原事务事实后，无条件设置`fact.unscoped=true`（87–90行）。原逐孔available筛选、可信子集以及子集合并全部删除。整盘待核表示尚不能证明影响范围，不是断言SQL实际改了每一孔。

按原触发顺序重新沿源码核对：

1. A可信、B未知仍可形成部分孔不可用的原冻结basis；这部分规则未改，也没有新增原任务的业务权限限制。
2. 真正`CommitUnconfirmed && !committed`被Root认领后，仍先登记`transactionUnknown`并调用foldTransaction。新的事务记录立即成为整盘，保持真实transaction与committed原值，不伪装成成功或回滚。
3. [`syncTaskReagentDisplayUnknown:169`](C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/task_reagent_root_candidate/revision_02/candidate/06_programs/sn100_host/shell/task_reagent_shell.cpp:169)在180–185行读到该事实的unscoped=true，将整盘待核发给页面。活跃动作仍保留的可信孔列表不能把这个true重新降为false。
4. B随后由真实Ur或手工来源取得新Provider运行值时，原Source恢复只处理显示资料待核，不删除事务记录。页面接收unscoped后清孔子集并维持整盘遮罩；renderer继续遮住孔量、详情及相关项目次数。因此“B从原未知变为可信”的后续变化不再绕开C。
5. 动作收尾再次归档同一个key仍写unscoped=true；动作退槽后C事实独立保留。角色/身份改变后，同根遮罩按新页面身份重新下发；换到其他已知根不把旧根事实硬贴过去，但全局事务阻断仍保留。未知根及溢出仍沿原保守整盘处理。

这关闭了原报告的确定可达序列。所读没有出现用新Ur、手工来源、普通状态变化或B恢复缩小C的语句。

## 保留范围及未验证项

原事务key仍用Root认领的`epoch/id/packet/connection`；root、receiptId、payload SHA和真实事务字段均保留。32事实上限及溢出阻断未变。`foldTransaction`首句仍只在transactionUnknown时登记C；普通显示资料待核的部分恢复没有被改为永久整盘未知。

原设备回应、processed后才排页面专读、页面应答与实际读取分别归还、合法无显示basis任务仍可走旧后台、原Ur来源确认及N守卫，均位于逐字节未变范围。此次不重复宣称全片业务复审，只保留上一轮对这些固定源码的检查范围。

C仍只保存在当前进程的有界记录中，不证明跨重启核清或恢复。新对象增量证明在总协调交办时尚在进行，本报告未读取或推定其退出结果。无新COMMIT重复TR弱刷新、原g6/g15与remaining净差账等已声明差异没有在本修点中改变。实际显示交错、错误恢复、RK和真实仪器结果仍须另行取证。
