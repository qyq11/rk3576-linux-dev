# 第07批：构建、同板暂存与匹配归档工具

本套工具只负责把已正式晋级的同一份源码完整交叉编译、核包、保存到板 B 的独立版本目录，再固定匹配源码和证据。它不切换 current，不启动/关闭上位机或主控，不连接业务数据库，也不安排电脑产品测试。正式晋级由 project_copy 单独提供的工具负责。

根已明确确认最终整合元数据 `integration_07_review/final_merge_review_with_shutdown.json` 的 SHA-256 为 `2b4730874ad721db6b66ed44195e2f202adac4e95ee697b23f5062c0b2599526`。实际表内底稿 649 项、最终源码 663 项、四片补丁、39 项变化（含 14 个新增、无删除）。这些是该份元数据的实际统计；工具从表和实际输入动态读取，不把这些数量当作后续固定常数。

## 固定范围

- `integration_07_tool_support.py`：共用的相对路径、普通文件、指纹、完整晋级和阶段终态核对。
- `build_integration_07.sh`、`verify_integration_07_inputs.py`：只使用现有 sysroot，以 Release、jobs=2 完整编译 ARM64 主程序及报告辅助程序；核真实输入、实际对象、依赖及包身份。缺配套输入即停，不恢复或重建 sysroot。
- `stage_board_b_release_07.py`、`.sh`：固定有线 `root@192.168.100.123`、板 B 专用 SSH 公钥记录及序列号 `8314b1e8ffd0321f`；要求 current 仍为 `releases/14ce9714aaa0017c7ad78ff3f551d978672e5def79b96e817605295d7e59020c`。上传通过同身份保护的 SSH 标准输入流，以排他方式创建新 UUID 包；上传和暂存分别保留实际返回码及日志。只建立独立版本目录，上传脚本原文也归入输入记录。
- `archive_integration_07.py`：暂存真实 0 且完整终态通过后，才新建第 07 批正式程序/匹配源码归档。README、来源和校验清单一次生成，逐份回读两个压缩包。
- `verify_and_save_integration_07.py`：重新核完整链，再新建第 07 批证据目录。同一来源可以同时保留根级原文和 inputs 内副本；按目标保存、先建父目录，不用源 Path 字典合并覆盖。
- `finish_integration_07_archive.py`：只读复核正式包、完整证据、阶段完成记录及两压缩包的 Git LFS 属性，最后新建收口记录；不改已有 README、来源或校验清单。

所有正式源码、快照和 checkpoint 均按元数据的实际路径解释。补丁路径是仓库根相对路径，patches 动态逐片核对；晋级终态必须 complete=true、phase=complete、current_patch=null，原片次、预演及正式应用顺序、最终文件表、根属性和脚本指纹全部对应。第 06 批属性、网络或归档器失败恢复不是第 07 批前提。

## 执行前还需根固定的值

`integration_07_tools_config.json` 当前仍为 `frozen=false`。根确认的 final_review 已填；晋级脚本和真实完成 progress 尚未生成时，不猜它们的 SHA，不把它们当成通过。

根实际完成独立晋级后，提供 promotion_script 和 promotion_progress 的精确仓库相对路径、SHA；本窗口将它们与最终工具、证据清单一起填入配置，才把 frozen 改为 true。配置本身 SHA 与最终整合元数据 SHA 是两件事。构建还会把该份真实 progress 原文保存为 merge_inputs.json，并绑定前后相同指纹。

两类前期失败原件已在 preparation_failures 中分别标识：原普通补丁行尾检查、attempt03 元数据失败。它们必须同时存在于最终元数据与 evidence 清单，按原文保留，不改成首次成功。

## 由总窗口执行的顺序

以下路径均相对仓库根；前一步实际非 0 时停止，保留原件，不部署旧包，不自动重试或覆盖残留目录。

1. 根独立审查并执行晋级工具，回收实际完成记录；据实际指纹完成配置冻结。
2. WSL `Ubuntu-24.04` / `qyq` 执行 `bash temp/sn100_host/workflow_night_20261102/build_integration_07.sh`。只有真实 `build_exit.txt=0`、完整输入/依赖/包终态对应，才能继续。
3. Windows 用已有可用 Python 执行 `stage_board_b_release_07.py`。核 upload 和 stage 两个真实退出 0、stage_complete 和板端包/两程序 SHA，仍不切换或运行。
4. Windows 依次执行 `archive_integration_07.py`、`verify_and_save_integration_07.py`、`finish_integration_07_archive.py`，每一步都回收真实工具退出码，并核独立 complete。正式程序与证据目录只新建，数量按实际清单统计。

构建、日志和阶段记录在 `temp/.../integration_07/`；正式恢复包在 `07_backups/sn100_host/board_b/workflow_integration_20261102/integration_07/`；证据在 `04_references/sn100_host/workflow_night_20261102/integration_07/`。这三处由对应工具排他创建；本次准备没有创建它们。

## 本次实际检查

只完成六份 Python 文件的 AST 语法解析、两份 shell 文件的 `bash -n`，以及根已批准元数据的只读结构/指纹检查。第一次 `python.exe` 命中 Windows 应用别名而退出 1，没有执行语法解析；随后改用现有 Codex Python，六文件解析实际退出 0。两份 shell 语法检查各退出 0，WSL 启动时已有 localhost 代理提示保留原范围。具体工具结果和来源见 `integration_07_tools_validation.json`。

交叉源码核对修正了两处工作目录依赖和一处暂存 started 格式名称不一致，接口现已对应。没有执行正式晋级、build、stage、archive、collector 或 finish，没有运行电脑产品或连接板卡；也没有新的 RK 内核或业务运行验证。工具定稿后仍由根安排另一位独立只读审查，再由根执行。

N 液面回报在途时一次退出仍待真实条件验证；不能把空闲关闭或本套构建归档工具通过说成该业务已实测。拟合、反算、ISI/INR 和质控判定不在本工具范围。
