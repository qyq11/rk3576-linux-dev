# 第06批工具交接

这些工具把已审八片修改晋级、完整编译，再把同批程序和匹配源码保存下来。初版交接形成时只做源码和语法检查；之后根执行首次晋级，遇到下面的换行属性差异。本次恢复工具仍只准备和检查，由根显式执行。后续成功范围以根协调者执行所得日志和真实退出码为准；工具准备完成不代表RK业务或真实仪器通过。

本批从第05批固定的639项源码开始，底稿清单SHA-256为`e785702f907aecee9bc645614f122c05f1c6053d33ecb1b94f7e08ee8eaaf111`。根已确认最终`integration_06_review/merge_review.json`为`e48ec9c5d5c3ffa9ee63f248dad9482536aebc7a48b871c6e00b1941513a13fa`；生成器仍要求执行者显式传入这个值，不能使用浮动清单。八片顺序固定在生成器；10项新增文件为通道4项、复制5项和保存通知头1项，保存通知头位于`projects/`。

## 当前恢复顺序：不能重跑首次晋级

首次正式晋级的前3片已记完成，第4片实际已写完；核对遇到`projects/copyprojectdialog.ui`换行差异而停止。正式目录按根`.gitattributes`的`-text`规则保留原候选LF字节，temp原先未镜像该规则，被Git自动换成CRLF。根已从第05批源码包在带相同规则的审计目录重演全部八片，证明正式恰等第4步，差异仅temp表单换行，并只对齐temp。原工具、失败记录、对齐前表单和清单保留。

根审计metadata为`temp/sn100_host/workflow_night_20261102/integration_06_attribute_recovery_01/audit_metadata.json`，SHA-256为`9b5aad0c47a81172e44e9cdf52acfa24e21688fa1b99c2cb4a4afcaa4de274b5`。在与该审计一致的Windows Git环境执行独立恢复工具：

```
python temp/sn100_host/workflow_night_20261102/resume_integration_06_after_attribute_alignment.py --audit-metadata temp/sn100_host/workflow_night_20261102/integration_06_attribute_recovery_01/audit_metadata.json --audit-metadata-sha256 9b5aad0c47a81172e44e9cdf52acfa24e21688fa1b99c2cb4a4afcaa4de274b5
```

上面的工作目录为仓库根；`python`指已核可用的Python 3解释器，不是Windows商店占位入口。工具核全份证据、原failedprogress、原晋级工具、八片、三处路径属性、0—8每步649目标和正式第4步后，才登记第4片已应用，正式只续第5—8片。不重打前四片、不回滚，不改对齐审计或stage。中断保留在途状态，拒自动重跑。

根此前误尝试的构建被输入检查挡下，实际退出1，只有4份输入/日志和3个目录，没有对象或程序。恢复工具先核全部文件、无产物、无链接且两个绝对路径均在任务内，再把它改名保留为`integration_06_prebuild_blocked_before_attribute_recovery/`；不删除。移动意图先写入恢复记录。恢复完整成功后，根才能重新执行原`build_integration_06.sh`，随后按下文第4—7步归档和板B保存。新verify显式核恢复metadata、原失败、恢复终态与保留的预构建证据；旧版`5460839f...`工具保存在`verify_integration_06_inputs_before_attribute_recovery.py`。

归档同时保存原失败、对齐诊断、每步清单、对齐前UI/清单、独立恢复工具与旧工具，以及首次被挡构建的4份证据。第05批源码包只核来源并保留引用，原归档不变。不把恢复成功写成首次晋级成功。

根实际执行恢复已退出0：第4片没有重打，仅续第5—8片，最终649项正式文件与对齐stage逐字节一致；旧预构建目录按核查后改名保留，新构建已启动，完整构建结果仍等真实终态。恢复工具保持`c04b16bf9fdd934042eba2c4836d0f4efc7d0f7490951ce000e28fc687c3d5d7`，原promoter保持`34dfbbd41c81ef7c1d7a51a700007c2788c8fa8e652b8fc6ba31a46ff70d01a6`。

独立窄审发现，输入核对的恢复字段缺失分支原会退回普通完成检查；本次必须带完整恢复链，不能退回。根已保留`verify_integration_06_inputs_optional_recovery_83d9.py`原件（`83d9e125613e846b0c7a545255d72b96fc05efb9608e9d9b86e9577abe294569`），只把该分支改为明确拒绝；最终执行版`verify_integration_06_inputs.py`为`7c064393c474f93400bf85888e3a4954f7be055a25a726e494e9b0b5bc0506de`。工具归档同时保存旧83d和最终7c；没有改正在执行的verify、build或失败原件。

## 首次执行设计（历史步骤，当前禁止重跑第1—2步）

在仓库根目录，用已核可用的Python 3解释器执行。构建脚本在`Ubuntu-24.04`、普通用户`qyq`下运行；板B上传沿第05批已有的SSH身份和已核主机指纹。不要运行本交接去替代逐步检查日志。

1. `python3 temp/sn100_host/workflow_night_20261102/prepare_integration_06_promotion.py --merge-review-sha256 e48ec9c5d5c3ffa9ee63f248dad9482536aebc7a48b871c6e00b1941513a13fa`
2. 核对生成的`promote_integration_06_review.py`指纹，然后显式执行它。生成器拒绝覆盖已有输出；晋级脚本先普通`git apply`到新的temp预演目录，与隔离副本逐字节比较，再逐片写正式，并在每片写入前持久记在途状态。底稿全部文件和新增/删除目标均核对，保留无关修改。中断或失败保留记录，不自动重跑或回滚。
3. `bash temp/sn100_host/workflow_night_20261102/build_integration_06.sh`。构建拒绝已有`integration_06/`，使用第05批同一sysroot；缺已有配套库即停止，不恢复环境。输入核对由脚本自动执行：全源码快照必须等于晋级的最终全表，再核实际构建列表和10项新文件。源码和实际构建输入数量从文件动态统计，不沿用旧348项；根当前审阅数为649项源码和357项实际输入。
4. 构建实际退出0、`build_exit.txt`为0、日志含`ARM64_INTEGRATION_06_BUILD_AND_PACKAGE_PASSED=1`、`build_complete.json`存在且通过后，执行`archive_integration_06.py`。它只新建`07_backups/sn100_host/board_b/workflow_integration_20261102/integration_06/`，逐份核源码并保存程序包、源码包、来源和指纹。失败不把旧产物当成功。
5. 显式执行`stage_board_b_release_06.py`。它上传后使用本批`stage_board_b_release_06.sh`，核已知SSH身份、板B硬件序列号`8314b1e8ffd0321f`、包与每文件指纹、两程序及实际动态库。只放独立版本目录，不切current、不启动程序、不重启主控。先写`stage_started.json`；失败或中断留下日志和在途记录，拒自动重跑。
6. 板B保存实际退出0后，执行`verify_and_save_integration_06.py`。它回读源码包全部文件，回读程序包全部清单和两程序指纹，核板B日志的批次/序列号/版本/原current和依赖结果，再新建`04_references/sn100_host/workflow_night_20261102/integration_06/`保存八片、独立审查、实际工具及日志。不会夹带第05批旧板端诊断作为本批结果。
7. 执行`finish_integration_06_archive.py`，按实际统计写两处新目录说明、复算全部字节清单，并核两压缩包的Git LFS属性。公共README、计划和记录由根统一维护；工具不提交Git。

## 核对边界

构建完成标志只在主程序、报表辅助程序、静态依赖、打包和产物指纹核对全部通过后形成；退出钩子保留真实失败码，若意外退出0但未完成则改为125。`build_complete.json`还绑定完整源码、晋级记录、双程序、依赖和程序包指纹。它不证明程序启动或实际业务通过。

本批程序保存保留原`package_arm64.py`打包范围，没有改打包白名单。全部新增文件位于原递归保存的目录中；通知头是间接引用，由完整快照覆盖，另外9项进入实际构建列表。没有修改正式构建文件之外的已有未提交工作。

归档不含真实数据库、INI、患者、账户和认证材料。复制故障留下的自有克隆和清单由复制功能的恢复判定管理；这些工具不删除数据库或关联行，不把本地复制成功当成共享对端同步。普通保存/批组的新生产入口与N校准点接收不属于本批。RK界面、业务、资源、真实仪器和长期运行仍待原配置联机验证。
