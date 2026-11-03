# 第09轮工具preparation_02最小重定基复核

2026-11-03，只读比较已审`preparation_01`与新`preparation_02`、Root修订03的来源元数据及正式源码。没有执行工具、编译、产品、测试、网络或板卡命令。

**结论：最小重定基符合本次范围，可在正式build03和官方归档实际完成后调用新目录的绑定/构建驱动。**本结论不代表主程序构建、检查器构建、板端或真实仪器通过。

## 实际比较

- 13份观察/运行逻辑与01逐字节相同：`ad_wire_observer.h`、`build_review_09.sh`、`capture_application_exit.sh`、`collect_case_09.py`、`current_alarm_observer_09.h`、`lis_connection_observer_09.h`、`observation_scope_09.json`、`review_actual_ad_09.py`、`run_case_on_board.sh`、`shutdown_contract_09.py`、`status_login_review_09.cpp`、`summarize_run.py`、`task_shutdown_contract_09.py`。上一轮三个修点及原24/可选、Qj后一次Qk、LIS只观察、真实关闭、原RX独核的已审逻辑没有重写。
- 可执行代码的差异只有：`bind_build_09.py`允许工作目录02→03；`execute_final_09_build.py`的EXPECTED_WORK02→03；`freeze_release_09.py`的批准review SHA改为本轮03实际SHA；一次性`prepare_tools_09.py`的PROMOTED来源和工作目录替换规则改为03。说明中的目录也同步为03。
- 三份来源元数据已换成Root03实际输入：批准审查、晋级输入、684份源码表。批准审查与源码表的复制品SHA、字节数都等于Root原件和晋级输入中登记的值。批准审查内的`final_source_files`等于独立源码表。
- 684份正式文件逐份SHA均与新表一致，缺失0、不符0。相对已审旧后像，唯一正式源码变化是`alarm_history/alarm_history_page_current.cpp`加入`#include "ui/cusmessagebox.h"`，没有业务规则变化。
- `candidate_sources_02.sha256`所列文件逐份匹配。新增`rebase_result_02.json`记录原件保留和本次来源变化，没有用新材料覆盖01。

## 绑定值与未生成项

批准审查SHA为`0e5c8a4a1f29b059c3ddd19ff1112c2f9881692c192a21b5134f708e9ce393c3`，与freeze里的精确守卫一致。新源码表SHA为`b0667063407c2534ece08e4063cf1e63c3a25cb8c6212066ad966a9c5d492815`。

当前`final_release_id`、`final_object_count`、官方源码包SHA和`actual_check_count`仍为null，compiled/product_executed仍false。新目录没有`frozen_09`、`prepared_inputs.json`、检查器二进制、`execution_09_result.json`、`review_release_id`、最终handoff、上传清单或关闭合同。这些只能由真实build03输入到齐后产生，不能拿01/08产物替代。

## Root实际执行链

以下是后续执行入口，不是本次已运行命令。先等待build03真实0及匹配官方归档完成，保留任何先前失败目录；取本次`build_complete.json`的release和官方`provenance.json`的源码包SHA后，普通WSL用户执行：

```bash
sn100_repo=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
sn100_work="$sn100_repo/temp/sn100_host/workflow_night_20261102/integration_09_revision_03"
sn100_tools="$sn100_repo/temp/sn100_host/board_b/20261103_status_login_09_preparation_02"
sn100_official="$sn100_repo/07_backups/sn100_host/board_b/workflow_integration_20261102/integration_09"
sn100_release=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["release_id"])' "$sn100_work/build_complete.json")
sn100_source_sha=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["source_archive_sha256"])' "$sn100_official/provenance.json")
python3 "$sn100_tools/execute_final_09_build.py" --execute \
  --integration-directory "$sn100_work" --release-id "$sn100_release" \
  --source-archive "$sn100_official/sn100_sources_$sn100_release.tar.gz" \
  --source-archive-sha256 "$sn100_source_sha"
```

驱动自己重新核真实正式完成记录、完整源/对象/依赖、官方包和原main，再串行freeze、链接检查器及静态依赖；不把读取到release或输入408行当作构建完成。只有真实驱动退出0及最终handoff绑定正确，才由Root安排上传、flat版本选择和一次授权观察。

板端运行前仍在同一次派发里核板B固定指纹/序列、唯一原主控、current及固定配置摘要清单/3INI+4主控配置SHA。使用新的独立attempt，不覆盖旧08或失败证据，不建LIS服务器。收集后另外实跑本目录`review_actual_ad_09.py`，保存真实退出和当前告警独审状态；collector0不能代替独审或真实业务通过。板卡操作现已由Root统一负责，本代理未触碰。
