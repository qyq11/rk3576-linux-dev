# 第08批构建输入清单的窄修复候选

本片只修构建输入清单：原七个任务剩余量源文件在“只列清单”退出后才追加，因此真实编译会使用180个源文件，旧清单却只列173个。候选把原4+3追加段连同注释原字节移到输入存在性检查之前，并在同处显式登记四个配套头文件。这样只列输入和真实编译使用相同登记；缺失的配套文件也能在编译前报出。

只修改`candidate/06_programs/sn100_host/shell/build.sh`，正式源码、旧构建目录和冻结清单均未修改。源文件追加段原537字节完整搬移，原LF行尾保留；源的相对编译顺序、业务、优化和编译参数不变。四个头文件均不含`Q_OBJECT`，不会引入新的Qt自动生成编译单元。候选第245–253行完成登记，第254行开始核文件，第257行进入只列输入分支。

底稿严格取正式08的30cf完整674目标表，先逐项核SHA后复制。正式目录中另有229个既有说明、独立模块、tests及pyc文件，它们不属于本批674目标表，全部保留并保护，不复制进本片；不能把正式目录903个现有文件误说成全在本批源表内。

实际完成：Python语法解析0，`bash -n`0，`SN100_ARM64_LIST_INPUTS=1`0；WSL Git普通check0/apply0，674份完整回演字节与候选相等。`preparation_result.json`保存真实命令、输出指纹和范围；每一步另保留`.inflight.json/.stdout.log/.stderr.log/.exit/.result.json`，驱动最终实际0。未编译、链接、生成表单或产物，也未运行产品、SQL、联网或板端操作。

只列输入显式使用已有配套环境`/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/rk_prepare/sysroot`，`build_inputs.tsv`为原始输出。实际结果为180 source、139 header、49 form、11 resource、18 helper_source、29 include，去重381份构建文件。七个源文件和四个头文件均各恰好一次。指定的`not_created_build_dir`与`not_created_output`都没有创建；此模式没有恢复或重建sysroot。

补丁为`build_inventory_fix.patch`，只含一个目标。674前后全表分别为`source_manifest_before.json`、`source_manifest_after.json`，实际回演表为`replay_source_manifest.json`。前表SHA为30cf6f075afaea1f5c6d5842759e63747abd8d7d0403f0e85f05ed019ae0346e；后表及回演表SHA同为097e538263882ba23e405427140729f5316f0c839aeb1d4582045d658be9e768。补丁SHA为adc3f29f4638bb87ae72a72d59fcdce28ac6d637749a4a7661356c698c0f95da。脚本前像97d5844e0d06cb4269ea8f75f8caee6b893d4a70d9e335b6715775d50cbbac8b，后像19edce78e5091293dc8d35a9b5fc37f50144acdf17d61e710d3e37cc4f639477。

`protected_inputs_before.json/after.json`均列1095份实际原件SHA，包含正式目录903文件、旧integration_08全部已有文件、原冻结配置/晋级记录/最终清单和本次首次失败原件，前后相等。旧配置77a、旧晋级95b、旧清单e2a7均保留原字节；本片不是重跑或覆盖旧integration_08。

首次准备工具误把整个正式目录903项要求与674目标表恰好相等，前置真实退出2，未创建候选源码或运行Git。原`prepare_candidate.py`、`driver.*`、`preparation_stopped.json`保留；`preparation_attempt_01_failure.json`说明纯工具范围错误。另新`prepare_candidate_v2.py`按674明确目标逐项核对后实际完成，`driver_02.*`记录真实0。没有改源表、删原件或掩盖失败。

后续由总协调串行审定并作为正式第六片合入，再用新的修订清单和独立构建目录完整ARM64构建。当前只证明构建输入清单和补丁字节一致，不代表编译、RK运行或真实仪器验证通过。
