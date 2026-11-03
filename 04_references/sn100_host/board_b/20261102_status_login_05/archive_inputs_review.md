# 第 05 轮状态与登录证据：归档输入只读盘点

## 本次结论与范围

原始板端采集清单的 41 份文件已逐份与电脑副本核对，41 份一致、缺失 0、差异 0、路径越界 0。第 05 轮 639 项匹配源码包的真实路径、文件内容 SHA 和包内条目数也已核实。检查器实际构建记录明确指向最新 `73d54a78…` 源码和 `8b48f0cf…` 二进制，不是旧 7c 检查器。

本子任务只读取本地文件和校验值，读取压缩包目录但未解包写入；没有网络、上板、运行产品或修改原件。只新增本记录。正式复制和打包由父任务完成，本记录不宣称新归档已经生成或已经通过最终回读。

所有仓库路径均从 `C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/` 起算。父任务本次目标为新证据目录 `04_references/sn100_host/board_b/20261102_status_login_05/`；恢复输入只在 `07_backups/sn100_host/board_b/status_login_05_20261102/` 内增加，不改旧 7c 或原第 05 轮保存原件。

## 原 current 恢复输入只能用于程序指针

现有 `07_backups/sn100_host/board_b/status_login_05_20261102/current_before.json:2–8` 记录：

- 捕获时间 2026-11-02 23:58:48.764756 +08:00；板 B，序列号 `8314b1e8ffd0321f`。
- 原 `current` 为 `releases/7c4abe3062ead1f5b6fc6fd495b708be5f11436504bba5e900b6ccb2c4f5d5f9`，属主 `0:0`。
- 本次预期切至 `releases/e31491a11e2e51764ab49c2c4435ce2f6f1f136223a76323d508ed613a758270`。
- 只保留 `current` 符号链接的原目标和属主，不覆盖原配置或业务库。

本次读取时该原件 SHA-256 为 `4bc683291814fea6f56fcd7358f992187f1186a991dd11572fbc266f40c2bfef`，现有 `SHA256SUMS` 与之相符。现有 `README.md:1–3` 也限定为“程序指针恢复输入”，SHA 为 `ac856d877d588f62f6cf194e4aee4ad6e1dc3f69912cc8e0c86c41397d4d2783`。

这个旧目标用于需要时恢复程序选择，不能当成新检查器的编译来源或本轮实际测试版本。恢复范围不含业务库、账户、配置、报警数据、主控进程或整机环境；不得因新证据通过而改写这份切换前原件。

## 正确的 639 项源码包与第 05 轮应用

第 05 轮匹配源码包准确路径是：

`07_backups/sn100_host/board_b/workflow_integration_20261102/integration_05/sn100_sources_e31491a11e2e51764ab49c2c4435ce2f6f1f136223a76323d508ed613a758270.tar.gz`

实际读取文件 SHA-256 为：

`fada1890e24e6d390af1e8c7216a18bde16b0c98619e76e9d9a67c8ab79d352c`

只读列包退出 0，包内恰好 639 项，路径前缀为 `06_programs/sn100_host/`。这与同目录 `provenance.json` 的 `source_files=639`、`source_archive_readback_files=639`、源码包 SHA，以及 `SHA256SUMS` 第 5 行一致。源码快照 `source_snapshot.json` SHA 为 `e785702f907aecee9bc645614f122c05f1c6053d33ecb1b94f7e08ee8eaaf111`。

同目录对应正式应用包是 `sn100_shell_e31491a11e2e51764ab49c2c4435ce2f6f1f136223a76323d508ed613a758270.tar.gz`，其既有清单/来源记录的包 SHA 为 `7ec88f87530ae3331a53dc679b4539256863726f0d78d61491876c5f43835e5d`，应用 SHA 为 `609aa4b9a60109df4df241181e4622626ab9b77fba2b272ca0643a5bcf5506dc`。新专项的恢复说明应指向这批匹配应用、依赖和源码，避免把检查器解到旧发布旁就假设运行组件匹配。

必须区分旧文件：`07_backups/sn100_host/board_b/status_login_20261102/matching_sources.tar.gz` 是旧 **7c 发布的 620 项**匹配源码包，依据其 `README.md:5,9–13`。它不是第 05 轮的 639 项包。引用名字“matching_sources”时不能省略版本和真实路径；旧包只用于旧 7c 恢复/历史证据。

既有 `workflow_integration_20261102/integration_05/README.md` 所写“23:43 只保存独立发布、未切 current/未运行”是当时的范围，原样保留。23:58 切换与随后自动联机结果应写入新的 `20261102_status_login_05` 证据，不能反写原保存时原件来消除历史阶段差异。

## 新检查器的实际构建记录

实际完成记录路径是：

`temp/sn100_host/board_b/20261102_status_login/integration_05_review_preparation/build_execution.json`

本次读取 SHA-256：`b26b1b3e6e0c2731d79046afd6254c0671d30b6b12d619b87c6cdeb587dd073d`。

该记录第 2–12 行明确保存：WSL 调用 `build_review_05.sh e31491a1…`、工具真实退出 0、261 对象、保留正式 main、编译前后候选源码 SHA 都为 `73d54a7802463d41c820dfbfd914612e2e78070b9f237ced60a2ec7bf8c8f11d`，检查器二进制 SHA 为 `8b48f0cf698cfaf8f1aeee40323e32ccc9201e2650072b217a25c34e1b1f1f05`。记录还说明旧 `d8fa…` 候选在首次绑定检查前已经替换。

新归档至少需要保留这份实际执行记录，并关联：

- 最新 `status_login_review_05.cpp`、`prepared_inputs.json`、`source_snapshot_05.json`；
- `build_review_05.sh`、`bind_build.py`、`link_provenance.json`、`review_binary.sha256`、`review_release_id` 和实际新检查器；
- 与本轮运行一致的 `run_case_on_board.sh`、`capture_application_exit.sh`、`summarize_run.py`；
- 关闭修订及独立审查记录，保留修前/修后版本标签，不把旧审查中的问题继续归到最新候选。

正式 639 源码可引用上述现有完整包及其逐文件快照，不需要用旧 620 项包拼补。当前用于检查器的 261 个对象逐份 SHA 已在链接来源清单中记录；重建时仍需同批完整构建，不能借归档名称混入旧对象。

## 41 份远端采集的电脑映射

原始清单路径：`temp/sn100_host/workflow_night_20261102/integration_05_live/auto_01/remote_capture.sha256`。

清单本身 SHA-256：`e4adac18fc6cc278eb80b0819f94c1cd61a238d6d3e79e0c432497b94c969334`。

清单中的路径都是安全的 `./相对路径`。它们对应远端运行根：

`/home/rkshare/programs/sn100_shell/verification/status_login_05_20261102/runs/auto_01/`

电脑副本根为：

`temp/sn100_host/workflow_night_20261102/integration_05_live/auto_01/`

映射规则是移除清单路径开头的 `./`，保留剩余目录和文件名，接到电脑副本根。逐份计算电脑文件 SHA 后，41 项全部匹配。

| 清单行 | 数量 | 内容及应保留的相对结构 |
| --- | --- | --- |
| 1–12、40–41 | 14 | 应用/封装/本轮退出码、日志、身份、资源、时刻、配置指纹及核对结果 |
| 13–21 | 9 | `results/` 内独立检查退出码、结果 JSON 和 7 张控件渲染图 |
| 22–39 | 18 | `runtime/` 内显示配置及原采集到的字体/图形缓存；原清单范围保持不变 |

归档复制时可保持同一 `auto_01/` 目录结构，再用这份原始清单回读；不要改写原清单路径或把不同目录同名的退出码摊平到一个目录。41 项不是 41 个功能检查，也不包含后来电脑端生成的说明。

`parsed_summary.json` 和 `independent_exit_scope_review.md` 是电脑端派生结果/复核，不在原远端 41 项清单内，应与原采集分清来源并另列校验值；`remote_capture.sha256` 自身也不列在自己的原文件清单里。它们可以一同归档，但不能声称是本轮从板端原样取回的 41 项之一。

## 新归档说明应保留的结论边界

新 `04_references/sn100_host/board_b/20261102_status_login_05/` 应串起：切换前后身份、最新实际构建、板端 41 项原始采集、电脑汇总、独立复核和仍待人工项目。新 `07_backups/.../status_login_05_20261102/` 保留既有 `current_before.json` 并补足本轮检查器有限恢复输入；不复制现场运行数据、实际账户/患者数据库或认证配置。

可引用本轮自动专项 20 项通过、四退出码 0 和正常线程收尾，但人工密码、实体键鼠、完整实屏、红色退出后重开、真实测试绿灯仍待验；Qq 通道提示、42/48 和 PT 波长差异不被本归档消除。资源仅 4 个短时样本，不写成预算、实时性或长稳通过。

父任务完成复制/打包后，还需要对新归档的文件清单、压缩包和源文件实际 SHA 做最终回读。该步骤由父任务执行，本只读盘点不替代它。
