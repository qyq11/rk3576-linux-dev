# Uh第12批正式采用前的即时只读核对

结论：本次核对结束、向Root发送结论之前，未见采用前阻点。正式源码按 `shell/package_arm64.py::snapshot` 同一文件范围原生枚举并逐项计算SHA-256，实际688项；与第11批b078清单相比新增0、缺失0、变动0。六份正式文件均与Root保存的before逐字相同，六份candidate均等冻结的局部后像。核对结束后Root可自行串行执行正式检查/应用；本报告不证明其后正式一直未变，也不替代Root实际采用日志。

本次只读核对及新增此报告，没有执行补丁、构建、产品或板端操作，没有改正式、Root/作者候选及公共文档；没有展开重复业务审查。即时结论发送给Root后不再读取正式六源，避免与其采用同时取样。依据为原生完整枚举/哈希结果与逐字节六文件比较，未把未取得结果的另一次只读采样记为成功。

正式前置清单：`workflow_next_20261103/integration_11_source_acceptance_02/lis_profile_source_retry_01/source_after.json`，SHA-256 `b0788f899b63a982c55ae85d098d7f4e843bdbedbd19b9f7c1ca0d5a3c569767`。快照规则文件为 `06_programs/sn100_host/shell/package_arm64.py`，固定摘要 `9fe7dab75b7566044d0ec485b0ffd47f4232be0142b664f35857dd6a18c524e3`。

Root候选根为 `temp/sn100_host/workflow_next_20261103/root_ascii_uh_candidate_12_01/`。以下原件当前实际摘要仍与已审冻结值一致：

| 原件 | 实际SHA-256 |
| --- | --- |
| `packaging_01/ascii_uh_on_b078.patch` | `d89e920d59e6fde53cb87a1290dc1a67483fb072fd38321071a9799d3ba71c15` |
| `sparse_after_6.json` | `67d0067d5b266435c82b775f078fe17ee5367bdcbfa7a0e2a58670d819dd218f` |
| `composed_expected_after_688.json` | `0e5c70ef9d5ac5fa3db34888103b4158783141aac5f90845e225c720d532ac8d` |
| 业务/守卫独审 | `f424f1d1a5db11c41deaf982276b8f6c4353dbdccdd6dd8d5826b631818ff32c` |
| 生命周期独审 | `42c3fb139d3b0453bf50ca522469aab3e62370cccade730078bb794950624c3a` |

`root_review_records.json` 所列双审实际摘要、字节数均吻合，分别9206、6468字节。`packaging_01/06.actual_exit` 与 `07.actual_exit` 保存的私有普通检查/应用均0；本审只读，没有重新执行。合成688预期仍只是下一批预期，不能算正式扫描或第11批源码。

以下六项逐项满足“正式等Root before、正式摘要等b078、candidate摘要等sparse_after_6”三项条件：`instrument/instrument_page.cpp`、`instrument/original_channel_receiver.cpp`、`instrument/original_channel_receiver.h`、`shell/device_control_shell.cpp`、`shell/frmmain.h`、`shell/main.cpp`。前后像具体摘要与九项守卫核对沿用本目录已冻结的 `ascii_uh_root_combined_business_review_12_01.md`，本报告不重写原审查。

第11批收尾原件 `temp/sn100_host/workflow_next_20261103/integration_11_01/finish_complete.json` 实际SHA-256为 `fdd4566958822c9bede9562152fa8918a04894f355a24e9597f9f7b0e1c74f99`，`complete=true`，发布号为 `356fe5f88efe52226105f68ec3d6479f70d3f4ad12bcb967da790a180d22b6d5`。已直接读取 `logs/root_stage_01.exit`、`root_collect_01.exit`、`root_archive_01.exit`、`root_finish_01.exit`，四阶段均0。finish绑定的collection_complete、archive_complete、object_binding及归档/证据两份SHA256SUMS，实际摘要均匹配其引用。

收尾原件同时明确 `current_changed=false`、`program_started=false`、`rk_function_verified=false`、`instrument_verified=false`。因此第11批收尾完成没有变成已经切换当前程序或仪器验证通过；板B仍按Root现有C7留屏要求管理。Uh不属于第11批，本报告只为下一批正式源码采用提供即时前置核对，后续编译、板端软件和真实仪器结果需另行登记。
