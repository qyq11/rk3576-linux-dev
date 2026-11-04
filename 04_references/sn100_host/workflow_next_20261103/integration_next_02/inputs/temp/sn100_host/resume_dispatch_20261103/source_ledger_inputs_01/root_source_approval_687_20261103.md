# 第10批源码来源核准：只用于本批构建

2026-11-03，唯一总控 `01a1a05a-73c4-7380-b761-c35a7fdbf76e`。

本批准只说明本批要编译的六片源码已经实际接纳、逐片后像已核。它不表示整套SN100移植完成，也不表示已编译、上板、接真实仪器或资源达标。板端仍是旧正式包b31f581f；20:38只读检查时仍为18:00启动的同一进程，原主控唯一，七份配置不变。

本批顺序固定为：ASCII显示/输入检查 → CAL盘面批号 → User04普通开始无写准备与配置同源 → LIS配置快照/固定发送对端 → 同程被动LIS观察 → 六正文手动ASCII入口与User04合并。最后实际687项表为 `integration_10_source_acceptance_01/ascii_b1/source_after.json`，SHA-256 `f741feea28fcb6a2bcff8d6c2bda49faa67d3b72e3508062e5f1e368b2422b8b`。各片普通check/apply原始命令和退出、完整前后表、独立审查与真实接纳脚本均须保留。

未采用的五份LIS纯字段文件、后续单孔Mg候选、真正User写入事务/Emergency和算法不在这次构建范围。`all_intended_source_slices_present=true`只指上面六片到齐。旧未执行合同中“User/LIS仍待补、还缺五字段”的文字按交付时点保留；本批准和冻结spec明确当前范围，不为凑完整而合入未采用文件。

本总控已读审未执行聚合器v1及v2增量，并接受独立审查 `source_ledger_independent_review.md`。批准本次运行的聚合器为 `source_ledger_candidate_v2.py`，SHA-256 `307674360d5c24c3fe17eae04e5c7a869f7f35021fdaabecb9fd0afedbc165cd`。相对v1只增加每片supporting_records真实读取/指纹核对，以及所在子目录的Root路径深度适配；不执行Git补丁、产品、构建或板卡操作。

输入底稿为未冻结 `root_spec.draft_with_provenance.json`，SHA-256 `39dc43ec403f3909f71f63e62db3f17910820ee58bf4bdc80c7c54351170cd25`。Root只会另写新冻结spec，填本批准及实际Git配置记录，不反写旧草案，也不在本批准中预填未来冻结spec/review/progress的SHA，避免互相依赖。

Root确认以下派生口径，原始记录保持不动：

- CAL两文件及User十文件首次普通apply成功，外层因行尾后像不符实际退出1。补正前字节和失败表、批准字节均已保留，逐份只差CRLF/LF；精确补正后的整表才作该片after。首次失败不能改作0。
- Root实际执行 `finish_user_exact_postimage_and_lis.py` 的工具结果为exec_command块 `07c46d`、退出0，输出分别记录User补正后d0ff4d57…和LIS三片c8bf26a4…。此事实由Root在执行后如实归档，不是Agent重跑，也没有另造该次原stdout文件。派生的correction_actual_exit=0指这个真实补正入口的最终退出；保留原acceptance未填该字段的事实。
- User派生inspection中沿用聚合器旧字段 `current_equals_failed_snapshot`，含义限定为“归档的补正前备份字节与原失败表相同”，不是说现在正式源码仍是失败后像。它须连同原inspection/失败表/备份/批准原件作为supporting_records实际核对。
- Observer原acceptance使用source_count=687，installer_actual_exit原为null。派生source_files来自该实际计数和完整后像；外层退出来自既有 `observer_install_04.exit` 的实际0。原null不改，应用退出、编译和RK结果不因此填0。
- Git实际只读记录为 `git_apply_environment_20261103_01.stdout.log/.stderr.log/.exit`；系统core.autocrlf为true。前三片保留实际历史argv，后续使用命令级 `-c core.autocrlf=false`。没有修改系统或用户Git设置，也没有改根.gitattributes。

旧09源码归档的实际SHA-256为 `6581e0e22406097ab5f2239df212d0569ef3a4ae65783c20a53a483057a5e7eb`；文件名中的b31f581f是关联发布号，不能当归档摘要。已流式核其684项源码与旧source_snapshot及本轮b066起表相同，不解包覆盖正式。

只有聚合器再次核全部输入、六片连续范围及正式snapshot规则下的687份实物完全一致，才能新建来源终态。其complete只表示源码来源聚合完成。实际完整ARM64构建、包指纹、板端只保存、切版和留屏观察继续由Root串行执行，分别记录；没有未来发布号、对象数、运行结果或退出码预填。
