# preparation_03最终窄复核

2026-11-03，只读比较作者`preparation_03`与已审`preparation_02`，并核现行26项清单和正式源码SHA。未执行候选工具、构建、产品、测试或板卡操作，未修改作者候选。

**结论：可以供Root在正式revision04完整构建和官方归档真实完成后，实际冻结、绑定并构建同版检查器。**未发现本次重基引入的确定阻断；本结论不替代真实执行结果。

## 实际差异与清单

相对02恰有5份文件改变：

| 文件 | 唯一变化 |
| --- | --- |
| `bind_build_09.py` | 精确允许的正式目录basename从03改04 |
| `execute_final_09_build.py` | EXPECTED_WORK从03改04 |
| `prepare_tools_09.py` | 旧模板目录02的替换目标从03改04；真实PROMOTED03源码来源保留 |
| `handoff.md` | 当前构建目录说明03改04 |
| `usage_09.md` | 实际调用目录03改04 |

其余20份原文件逐字节相同，包括全部13份已审观察/运行逻辑、批准审核、684份源码表、晋级输入和旧重基原件。新增`rebase_result_03.json`及现行`candidate_sources_03.sha256`，没有删除文件。

现行清单26个名称唯一，恰好覆盖目录内除清单自身之外的全部文件；逐份SHA核对0不符。清单SHA为：

`0f378c660bcc0a9f1e8e595d170be3da1018348c20d3b105e173d141be170b83`

保留的`candidate_sources_02.sha256`描述02历史。它在03目录对已重基的5个文件不再匹配是预期，`rebase_result_03.json`已明确它不是03现行清单；不能改写旧清单消除历史差异，也不能错用旧表判断03物料通过。

## 来源与未知值

批准审核SHA仍为`0e5c8a4a1f29b059c3ddd19ff1112c2f9881692c192a21b5134f708e9ce393c3`；freeze精确守卫保持一致。源码表SHA仍为`b0667063407c2534ece08e4063cf1e63c3a25cb8c6212066ad966a9c5d492815`，684份逐项等于批准审核和当前正式文件，缺失0、指纹不符0。此次没有新源码差异。

`final_release_id`、`final_object_count`、官方源码包SHA和实际检查数继续未知；frozen、compiled、product_executed均false。没有冻结目录、prepared/link/检查器二进制、最终handoff、正式关闭合同或上传清单。03部分对象不复用，不能从其进度或语法检查推出04完整构建成功。

## Root确切执行入口

只有拿到**revision04**真实`build_exit.txt=0`、完整`build_complete.json`和匹配官方归档后，普通WSL用户执行以下入口。release和源码包SHA从这次真实文件读取，不填写预测值；命令未由本次审查执行。

```bash
set -euo pipefail
sn100_repo=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
sn100_work="$sn100_repo/temp/sn100_host/workflow_night_20261102/integration_09_revision_04"
sn100_tools="$sn100_repo/temp/sn100_host/board_b/20261103_status_login_09_preparation_03"
sn100_official="$sn100_repo/07_backups/sn100_host/board_b/workflow_integration_20261102/integration_09"
test "$(cat "$sn100_work/build_exit.txt")" = 0
sn100_release=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["release_id"])' "$sn100_work/build_complete.json")
sn100_source_sha=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["source_archive_sha256"])' "$sn100_official/provenance.json")
python3 "$sn100_tools/execute_final_09_build.py" --execute \
  --integration-directory "$sn100_work" --release-id "$sn100_release" \
  --source-archive "$sn100_official/sn100_sources_$sn100_release.tar.gz" \
  --source-archive-sha256 "$sn100_source_sha"
```

驱动保持已有逐阶段真实退出、完整输入前后SHA、官方源包、依赖、全部同批正式对象和真实main的核查。只有驱动真实0且最终handoff绑定正确才继续板端操作。

后续仍由Root独占板卡，启动前在单次派发中核固定SSH指纹/序列、current、固定摘要清单和3INI+4主控配置、唯一原主控；使用新attempt，保留旧08与失败证据，不造LIS服务器或重复启动主控。收集后另外执行本目录`review_actual_ad_09.py`，分别登记真实退出和当前告警独审状态，不能以collector0代替。
