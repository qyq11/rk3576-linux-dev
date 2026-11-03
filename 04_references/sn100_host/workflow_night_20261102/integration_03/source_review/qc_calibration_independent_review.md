# 校准/质控任务第二片独立暂审（2026-11-02）

## 范围

只读 `qc_calibration_tasks/candidate/` 下原样本仓库、任务编码/检测会话、校准预设、独立校准占号/状态和页面接线，并对照原Windows可达函数。审查开始和主要读取时目录尚无handoff/manifest，工作聊天仍在准备，以下是暂审，不作为固定候选放行。

没有改候选或正式源码，没有编译、运行或操作Windows/RK界面，没有连接板卡、创建业务数据库或发送仪器动作。

## 已发现并确认修正的问题

原候选 `original_sample_repository.cpp::saveCalibrationParameters` 曾使用 `UPDATE project_base SET last_cal_para=? WHERE xuhao=?`。原项目是name/value键值表，last_cal_para/xuhao是键而非列，按该SQL会保存失败。

已通知总窗口；后续读到候选改为 `UPDATE project_base SET value=? WHERE name='last_cal_para'`，保持文件归属、锁及写入前指纹检查。该项在已读修后版本中已消除，不再作为当前缺陷。

## 第二项已发现并确认修正的问题

**点数到10以上时，校准点会按文本顺序落库。**

`original_control_tasks.cpp::reserveCalibration` 用 `QMap<QString,QList<int>> pointTasks`，键为 `calibrationId + '/' + calibrationPoint`，随后遍历map逐点INSERT。因此同曲线的点1、10、11、2按文本顺序创建数据行。自动稀释预设允许最多128点，条件可达。

原 `frmsample/frmsamplematerial.cpp` 的完整/简化校准路径用 `QMap<int,CalibrationResultData> caliDatas` 按数字点序插入；原 `CalibrationDataTable::queryDataByID`（calibration_data_table.cpp:95–96）及queryResultDataByID（:176–177）明确按id读。当前Linux校准页按point_index,id读可掩盖此差异，但原Windows会沿写入顺序读取。建议改成 `QMap<QPair<qint64,int>,QList<int>>` 或数值嵌套map，保留每点only_count内重复任务的原下发次序。不要通过改原点编号或事后重排任务号解决。

已通知总窗口；暂审文件写完后重核，候选已在:95改成`QMap<QPair<qint64,int>,QList<int>>`，:120用qMakePair形成数值键，文本点序问题已消除。上述描述保留为修正依据，不再作为当前缺陷。本项是源码行为差异，不以整机尚未验收或本机未跑测试充当缺陷；固定候选后仍须按最终指纹重核。

## 主干已核对的部分

| 事项 | 原依据与候选情况 |
| --- | --- |
| 校准独立计数 | 原CalibrationTotalCountTable明确表名拼写`calibraiton_total_count`、列total_count；候选保留该表，读取唯一计数并核不得覆盖既有calibration_id |
| CAL与普通报告分开 | 原slotSendRackTasks先组CAL再普通/QC；候选先生成CAL，reserve为CAL分配全局taskId但reportRow=0并跳过report_data/report_results占位，再在独立calibration.db写base/data |
| CAL基础/点/重复身份 | 原一架同项目共用一个calibration_id；point_index为reportRowID+1。候选按项目分配一曲线，point_index1起，only_count保存该点全部重复taskId，回包按taskId找唯一槽位，而非简单追加到末尾 |
| 既有曲线保护 | 新占号先核最大calibration_id与counter；回包calibrationState同时核点、任务id、比例、浓度、项目、校准品批号及未审核。已有数值/编辑内容不被“待计算”覆盖 |
| QC编号不混淆 | 原样本类型Quality=2，与报告类型kQualityData=1属于两套字典。候选保持sampleType2，在report_data存type1，没有把样本枚举直接写入报告type |
| QC身份 | 原quality_control_list/quality_control_batch/quality_item_proj按id/归属读取；候选带dataset/control/batch/association身份及原名称，保存和下发时重核项目关联；实际结果文件保留这些rk_quality快照字段 |
| CAL新旧选瓶 | 原完整协议在related且选择组非Default时用selectedGroup.contains(瓶批号)，简化LinuxNew不按校准批组选瓶。候选assignCalibration仅在旧分支筛瓶，并临时关闭普通样本组分过滤再恢复project副本；selectedGroup作为本次批组发出 |
| CAL Cleaner字段 | 原CAL在普通IBS位置发Cleaner孔；候选taskFields按CAL选择Cleaner，SAM/QC仍用原IBS；新旧协议字段差异保持 |
| 浓度精度 | 原自动校准保存重新用原基准浓度×比例/单位倍率，未取表格显示的舍入数；候选同样使用原基准输入。手动则逐点读取输入并换算。不能把显示四舍五入当作实际存储精度 |
| 收曲线仍待计算 | 候选写真实payload、曲线、参数/单位快照及rk/calculation_state=waiting；QC只更新对应type1报告状态，不写quality_data伪数值；CAL写对应占号点“待计算”且flag5，不给正常结果0，不审核/应用曲线 |
| TR融合 | 新任务记录沿用reservedReagentVolumes与原瓶身份记录；TR先核整包、任务类型/校准点/孔位，再按是否已加试剂退还实际预留量。CAL只核真实点关联，不创建普通报告或伪造校准数值；重复TR仍不重复退量 |
| 曲线重复保护 | storedResultPayloadHashes_仍在原文件、对应数据库状态和样本预设均完成后登记；相同已完整保存回包直接确认，不重写新预设；不同hash拒绝。TR后迟到Tr、已有曲线后TR保护保留 |

## 固定后应复核

- 取得工作聊天最终manifest及准确源码指纹，重核修后的last_cal_para语句和数字点顺序；不把本暂审覆盖到之后的新代码。
- 检查交接是否明确算法未接、QC曲线未写伪结果、CAL点待计算，以及初始预设、taskId、calibration_id、point_index、repeatIndex各自用途。
- 最终共享文件应以总协调已合入的TR/同步版本为基线，不用sync_base旧副本覆盖新保护。特别保留storedResultPayloadHashes_、TR瓶身份、重复退量及连接不明限制。
- 原校准库只支持候选明确检查的SQLite多库事务模式，不能为通过占号而改现场journal_mode。条件不足标待联机，不视为代码缺陷或擅自更改配置。

本轮未增加Windows运行、ARM64构建、RK业务或真实仪器通过结论。源审查确认的正常调用链与待验范围分开；没有对未知现场资料作假设。
