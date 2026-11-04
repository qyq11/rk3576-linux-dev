# 第12批第二条运行链：新现场核心草案独审

结论：**所审新草案、现场映射和24字段目标意图没有发现确定的来源事实阻点。** 相比旧3446核心，只更新了新链命名、未冻结状态、对应原件摘要和实际相机身份；没有把旧链79拒绝改写为成功，也未预填新链停止、激活、启动或观察成功。这里只审来源事实，不批准生成、冻结或板端阶段。

## 绑定的起止字节

目录：`temp/sn100_host/resume_dispatch_20261103/runtime_batch12_02/`。

| 文件 | 字节数 | SHA-256 |
| --- | ---: | --- |
| core_inputs.draft_01.json | 9605 | `286951d5ad1e71549fc8020a0a4847d795dbada3c5883bf8d1f9c820ba03d526` |
| fresh_baseline_01.json | 2429 | `3ba052b2339d02707f5754e026df90de5fc080799dfca5b516607b3c5c2c28af` |
| fresh_baseline_provenance_01.json | 1415 | `6ed0aca5cfce93dd32fbb61fd6b6b9055fe8dc9d3ed04e200d5f3ac6812f8572` |
| stable_target_intent_01.json | 1748 | `cd27f7cfa2b6e09ce44501b73be254088b059db3ba08c1570e46fe2c336e1c8f` |

四份文件结束复核均未漂移。递归核对核心26条记录和来源说明5条记录，共31条，实物字节数/SHA全部相等；没有执行草案生成器或候选核对函数。

## 与旧核心的差异及实际依据

1. `frozen`由旧3446实际核心的true变为新草案false。transition、metadata、audit及observation/captures路径使用新的runtime_02，观察ID为 `integration_12_01_lis_02`；capture和目标意图都引用新目录中的新原件。旧runtime_01没有被覆盖。
2. 唯一现场进程变化是相机worker：PID `2046481→2510511`、启动计数 `5701959→6498209`，服务MainPID同步变化。相机UID0、exe `/opt/opto-camera/bin/opto-camera`、SHA `7a128358683d146319d6c79fccbfc10b144937083ca2647a2cd03355fe2ed4c4`、服务active/running保持。
3. 新来源说明固定 `runtime_context_integration12_20261104_03` 四份原件。实际log为2281字节、SHA `3ae7a40b68e0e0838391595535a34abc3c069220f00ac21eb77974ad429ad09d`；实际外层exit是原文 `0\r\n`。实际UTC是 **2026-11-04T02:36:09Z，即北京时间10:36:09**，不要写成10:36:09Z。`.started`原件是派发脚本摘要，并不是采集时间；本次时间依据是原log的UTC字段。
4. 本审查独立将原log中的MAIN/CONTROLLER/CAMERA/THREE_A各PID、启动计数、UID、exe、SHA逐项对到新capture，并核core与capture逐字段相等。C7仍669382/2102645、run20261103_221819_662833；同boot `0eb94389-f120-42ad-9042-9c21d96a1481`，current仍C7。原主控5008/118180、3A139148/847289、端点、原七配置ad11、三指针、原helper和latest_run摘要全部与旧记录相同。
5. 新只读脚本与上次脚本逐字相同，实际log另有新相机身份；脚本核旧run无exit_code、C7映像、七配置实际hash检查，原输出明确PROGRAM_SIGNALLED0、CURRENT_CHANGED0。这里没有由旧snapshot代填新相机或伪造业务退出。

`prepare_runtime12_core_draft_02.py`相对01仅改输出目录、context03前缀和runtime_02/观察ID命名；没有改实际阶段退出或来源取值规则。新核心file_phase、target、root_decision和source_templates与旧核心逐字段相等：这是已经实际完成的第12批文件阶段事实，不是新运行链未来结果。

## 24字段意图与未完成范围

目标意图仍恰好24字段，键顺序与旧版相同；内容仅变observation_dir和observation_id。它仍指69220b…发布、fb1d42…主程序、d09文件阶段cfg、真实build_complete和2834aa…包；supervise_seconds仍整数0，leave_running_required=true，四项动作许可均false。意图自身frozen=true只固定目标描述，不能代替核心或每阶段动作批准。

新核心没有新链阶段授权、停止收据或未来退出字段；主体仍false。旧链在发关闭前79拒绝及C7仍运行的记录必须原样保留，不能让新链覆盖。此报告没有审定“既有flat只读采用”的新prepare方案；该方案仍须作者冻结后独审，不能沿旧prepare的“必须不存在”入口硬跑，也不能把旧prepare成功回执移到新链。新链后续仍须逐阶段实时身份与配置守卫，不能把10:36现场当作无限期有效。

本次只读文件和自写报告；没有联板、生成、冻结、运行产品或修改任何正式源码、作者候选、旧证据。
