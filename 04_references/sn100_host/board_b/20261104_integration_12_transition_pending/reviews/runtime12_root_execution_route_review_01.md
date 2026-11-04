# 第12批运行切换：后续授权与原件回收路线

结论：先整理已实际成功的metadata原件，再依次生成、审查、唯一执行prepare→preflight→stop；只有本轮stop整阶段真实0及准确C7干净退出、显示释放收据齐全，才生成activate→launch→observe。**不重跑metadata，不预填未来退出或收据。** 本报告仅整理Root执行协议；没有生成脚本、联板、发信号或运行产品，旧generator review02保持原样。

## 已取得的起点

- 代码快照两py仍与review02相同：generator `6e786403…`，filephase `b5683da1…`；没有改源码。
- 冻结核心：`temp/sn100_host/resume_dispatch_20261103/runtime_batch12_01/core_inputs.frozen_01.json`，9604字节，SHA `3446ab38e1448c076b825efcdc2e6e2990600967d531fe00d24964a661d2bab7`。
- metadata授权：同目录 `authorization_metadata_01.json`，2029字节，SHA `1c255a5ac6021138ee9d339b51a42b3ea675492a8fd57d14617f63f72d5ce488`。
- Root批准原件：同目录 `root_runtime_approval_01.md`，2968字节，SHA `cfca391030215ff0ae8a973ef0feb1d5dd284e256f955e8b977ca7d1113a55df`。后续可继续引用此覆盖逐阶段操作的实际批准；每个阶段仍须新存独立阶段授权，不修改已执行metadata授权。
- metadata原log：`temp/sn100_host/root_manager_20261103/runtime12_metadata_01.log`，1085字节，SHA `4a7068d0edcef8d42b302c1d2a3e7a85030060eca5c2c02fa99ab6d5f23440dc`；首行为回执摘要，余下正是完整回执，SHA应为 `a398407d74bd06f4d0fd52eaa6e5a25d3f34adf29c44e5dadf7801d59e4f460f`。内容绑定C7 current、未启动/未关闭和本轮core。
- metadata原外层退出：同名前缀 `.exit`，原文是3字节 `0\r\n`，SHA `13bf7b30…`。这是真实0，但生成器319行仅接受2字节 `0\n`。

## 每一步统一的host文件协议

以下路径均为仓库根相对路径；记录中的path须使用正斜杠。

定义 `H = temp/sn100_host/root_manager_20261103/runtime_transition_captures/integration_12_01_runtime_01`，`A = temp/sn100_host/resume_dispatch_20261103/runtime_batch12_01`。生成器硬要求：

| 原件 | 准确host路径与内容 |
| --- | --- |
| 阶段P收据 | `H/P.receipt`，板端receipt原字节；LF、无CR、末尾一个换行，不能重排字段或重建内容 |
| 阶段P完整派发日志 | `H/P.log`，完整原stdout；末尾必须准确为 `RUNTIME_RECEIPT_SHA256=<实际摘要>\n` 加完整receipt字节；不得在尾部追加说明 |
| 阶段P真实外层退出 | `H/P.exit`，必须严格 `0\n`；失败如实保存，不得为生成下一步而改0 |
| 阶段P授权 | 建议 `A/authorization_P_01.json`；后续阶段引用此实际原件摘要，不改旧授权 |

本次读时H目录尚未建立。metadata可从实际原log首行之后按原字节提取receipt，验证摘要=a398…；log逐字复制。原CRLF exit保留，在H另存“将已核真实整数0规范为LF”的派生退出，另存来源映射，固定原path/SHA/bytes、派生path/SHA/bytes和仅换行规范化规则；不要声称它是板端原文件。原log本身已经是LF，无需改写。此后每一步沿用同一诚实回收规则。

每个原件记录的字段必须且只能为 `path`、`sha256`、`bytes`；bytes是整数，摘要由实际字节计算。空槽准确为 `{"path":"","sha256":"","bytes":null}`，不能填64个零。空stderr允许实际0字节及其真实摘要，不能为了非空要求往里加文字。

## 每份阶段授权准确字段

授权顶层必须且只能含下面15项：

`format, frozen, phase, allowed, core_inputs, root_approval, previous_authorization, previous_receipt, previous_phase_exit, previous_phase_log, stop_intent_record, old_stop_receipt, old_stop_phase_exit, old_stop_phase_log, old_stop_files`。

共用值：format=`sn100-integration-12-01-runtime-phase-authorization-v1`，frozen=true，allowed=true，phase为本阶段原名称；core_inputs逐字段固定上述3446…冻结核心记录；root_approval固定实际Root批准记录。其余按下表补齐：

| 本阶段phase | previous_authorization | previous_receipt / exit / log | 停止附加字段 |
| --- | --- | --- | --- |
| prepare | 实际metadata授权1c255… | `H/metadata.receipt`、`H/metadata.exit`、`H/metadata.log` | 四项stop记录和九个old_stop_files均保留空槽 |
| preflight | 实际prepare授权 | `H/prepare.receipt`、`.exit`、`.log` | 同上 |
| stop | 实际preflight授权 | `H/preflight.receipt`、`.exit`、`.log` | 同上；stop授权此时也不得预填尚未发生的自身结果 |
| activate | 实际stop授权 | `H/stop.receipt`、`.exit`、`.log` | 填本轮真实stop四项及九原件；previous_receipt必须与old_stop_receipt为同一记录 |
| launch | 实际activate授权 | `H/activate.receipt`、`.exit`、`.log` | 保持同一实际stop四项及九原件 |
| observe | 实际launch授权 | `H/launch.receipt`、`.exit`、`.log` | 保持同一实际stop四项及九原件；launch收据须给出真实新RUN/PID/start |

四项stop字段准确是：`stop_intent_record`=核心的 `stable_target_intent`（1748字节、SHA `8268b34817c269de06949ce8c3468c21fd4cbb0f94159c9048e89545a1ebb87b`）；`old_stop_receipt`=`H/stop.receipt`；`old_stop_phase_exit`=`H/stop.exit`；`old_stop_phase_log`=`H/stop.log`。不要换成activate新授权的摘要，也不要借b31旧停止回执。

## stop成功后九份原件采集协议

准确旧run为 `/home/rkshare/programs/sn100_shell/runs/20261103_221819_662833`；定义板端元数据目录 `M=/home/rkshare/programs/sn100_shell/verification/integration_12_01_runtime_01`。

Root仅在已核板卡身份、stop唯一执行完毕后，以只读字节方式回收下表文件，可原样存到 `H/stop_files/`。old_stop_files记录的host路径不是硬编码，但每一摘要必须与stop收据对应字段相等。先采集真实字节再算摘要，不从期望值生成文件。

| old_stop_files键 | 板端原件 | 回执摘要字段 / 必须内容 |
| --- | --- | --- |
| result | 旧run的`result.txt` | OLD_RESULT_SHA256；精确 `APP_EXIT=0\nCLEAN_SHUTDOWN=1\n` |
| exit_code | 旧run的`exit_code` | OLD_EXIT_CODE_SHA256；精确 `0\n` |
| stdout | `M/phase_logs/stop/stdout.log` | STOP_STDOUT_SHA256；原字节 |
| stderr | `M/phase_logs/stop/stderr.log` | STOP_STDERR_SHA256；原字节，可为空 |
| actual_exit | `M/phase_logs/stop/actual_exit` | STOP_TOOL_EXIT_FILE_SHA256；精确 `0\n` |
| app_pid | 旧run的`app.pid` | OLD_APP_PID_FILE_SHA256；精确 `669382\n` |
| app_start_ticks | 旧run的`app.start_ticks` | OLD_APP_START_FILE_SHA256；精确 `2102645\n` |
| app_exe | 旧run的`app.exe` | OLD_APP_EXE_FILE_SHA256；准确C7 `/home/rkshare/programs/sn100_shell/releases/c7cbf438a3a034b25ae22accfaf48bacf0717174ce85bef2203ce8db143cb06e/sn100_shell` 后加LF |
| boot_id | 旧run的`boot_id` | OLD_BOOT_FILE_SHA256；精确 `0eb94389-f120-42ad-9042-9c21d96a1481\n` |

此外回收 `M/receipts/stop.receipt` 与真实stop整阶段派发log/exit。九原件的actual_exit是“正常停止工具的退出”，H/stop.exit是“包含事后quiet核对和发收据的整个阶段退出”，旧run/exit_code是“业务程序退出”，三者不能互代。回执还必须有APP_EXIT0、CLEAN_SHUTDOWN1、STOP_TOOL_ACTUAL_EXIT0、DISPLAY_RELEASED1、准确旧身份、C7清单/helper摘要及CURRENT_AFTER=C7。

只要停止工具非0、应用非0/不干净、显示未自然释放、整阶段非0或没有完整stop回执，就不能生成activate。尤其“停止工具已0，但quiet失败”表示C7可能已经退出，不准重发stop或原样重跑阶段；保留全部失败现场，由Root按实际状态另审处理。

## 最短串行路线及易错点

1. Root只整理metadata的H三文件及规范化来源记录；核a398、core3446、授权1c255、原日志完整末尾和真实0，不重装元数据。
2. 新存prepare授权，生成到快照同级 `generated/prepare_01`，审生成物，再唯一执行并回收H/prepare三文件。它只从nested复制到flat，不切current。
3. 同法preflight；必须普通用户核新flat，C7继续存活。取得H/preflight真实0后，才新存stop授权。
4. 审并唯一执行stop；不用外层超时、重复TERM、停服务或强杀。成功后先回收上述三文件和九原件，逐摘要匹配，再形成activate授权。
5. activate唯一执行并回收三文件；它只把current切到 `releases/69220b2e…`，不启动。flat路径不能误写成 `releases/integration_12_01/…`。
6. launch必须承接实际activate回执，单次 `systemd-run→openvt7→runuser lckfb→supervise BASE FLAT NEW_RUN 0 ''`。等到实际新身份后回收三文件；服务请求0不等于程序功能通过，APPLICATION_EXIT继续not_evaluated。
7. observe只承接实际launch回执中的新RUN/PID/start；使用原被动等待器。其超时只让工具退出，保持应用，不自动停止、重启或回退。0与SHELL_READY等材料齐全后才记软件观察结果，原始RX仍须另核；此生成器不判真实Uh发送、LIS业务或仪器功能通过。

通用生成调用字段为 `--phase P --inputs A/core_inputs.frozen_01.json --authorization A/authorization_P_01.json --output <快照目录>/generated/P_01`；A/P占位须展开为上面准确路径。输出目录必须全新，既有执行/失败目录均不覆盖。每一阶段先有前一阶段真实材料，后存授权，后生成，后独审实际脚本，最后由Root唯一执行；不要批量预生成activate/launch。只有Root可执行，作者与本审查均无板卡动作。
