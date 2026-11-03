# 第09轮总控检查器与联机辅助静态审查

2026-11-03。只读审查四份总控辅助及其直接调用的preparation_03接口，仅写本报告。没有运行辅助、编译、网络或产品，没有修改正式工程，也没有操作正在全量构建的main04。

结论：按本报告列出的最新字节，未发现路径、交接字段、场景命名、真实退出保存或固定身份/七配置门槛的确定阻点。实际执行仍须等各自前置阶段真实完成，不能将本次静态审查当作检查器编译或板端通过。

## 对应关系与执行顺序

- `run_checker09_revision04.py`指向真实WORK `integration_09_revision_04`、正式09归档和候选`20261103_status_login_09_preparation_03`。它先要求build/stage/archive/collect/finish五个真实外层退出均0及finish完成，核官方源码包实际SHA与provenance，再核候选清单，才调用普通用户`Ubuntu-24.04/qyq`的最终检查器驱动。参数与驱动的`--integration-directory/--release-id/--source-archive/--source-archive-sha256`契约及明确EXPECTED_WORK一致。外层真实退出独立保存，不运行Windows产品。
- 本次实际逐份核`candidate_sources_03.sha256`的26项，均等于当前preparation_03原件。驱动实际生成的`formal_application_sha256`、`checker_sha256`、`upload_manifest_sha256`和8项`upload_files_sha256`等字段，均与总控live辅助使用名称一致。
- `prepare_live09_revision04.py`已按审查建议补齐交接互核：schema、checker_built、driver0、前后输入一致、本次build的release/app、实际检查器SHA，以及8项物料map和清单SHA。物料真实逐项核SHA后才打包上传；本地实际上传日志与退出码分开排他保存。
- 上传目的地为`verification/status_login_09_20261103`。生成的单次运行脚本使用`auto_revision04_01`，所以remote_case准确为`/home/rkshare/programs/sn100_shell/verification/status_login_09_20261103/runs/auto_revision04_01`，符合候选collector要求的日期和`auto_`命名正则。runner仍要求平层正式发布，不直接启动nested暂存版。

## 板卡、配置与切版门槛

- 上传通过已固定公钥的`board08`选项连接，发送脚本先核板B序列`8314b1e8ffd0321f`并取得共同生命周期锁。没有在这里启动检查器或主控。
- 激活辅助先要求五个真实阶段退出0、finish与build同release。板端脚本核序列、当前08指针、nested包及两程序实际SHA、唯一原主控的exe身份与5000监听，以及没有现存应用/检查器，再复制到原启动器支持的平层新目录并原子换指针；旧指针和身份记录保留。
- 激活脚本已补两份公开SHA清单的自身指纹，来源为08权限复核的本地固定归档；先核远端摘要清单原文SHA，再用它们核实际三份上位机INI和四份主控配置，总计七份。清单变化与业务配置变化都停止，不修改INI凑条件。
- 单次运行派发脚本同样先pin这两份旧公开清单并核七配置，再调用已有runner。runner还会重新核current、应用/检查器SHA、唯一原主控及实际生效INI路径与必要键，保持`ini_before.sha256`的lckfb组只读修正。生成脚本不等于已经执行这些检查。

## 实际派发、采集与原回包独核

`collect_live09_revision04.py`读取root本次真实`run09_revision04.log/.exit`，要求已包含对应RUN_DIRECTORY及WRAPPER_EXIT，才按原字节排他保存到`integration_09_live/auto_dispatch.log/.exit`。当前窄封装只接受实际派发退出0或1；网络、前置或其他退出先保留并诊断，不改成0。

随后调用preparation_03的`collect_case_09.py --execute`，传准确remote_case和真实预期派发/case退出。collector还须在远端核该case确实结束和case_exit相符；因此不会仅凭本地外层日志认作结束。若采集失败，保留其实际退出并停止，不进入独核。

采集成功后，本地case路径取同名`auto_revision04_01`，与collector的实际落点一致；再单独调用`review_actual_ad_09.py <case> <preparation_03> --output <独立报告>`。命令接口匹配，两个工具的真实外层日志和退出分别保存。原始通信回包核对、基础检查、LIS/当前告警证据及待验范围仍应按实际报告分别读取，不能由采集退出0代替业务通过。

## 本次锁定的辅助字节

| 辅助 | SHA-256 |
| --- | --- |
| `run_checker09_revision04.py` | `a832bf7b6899be29c9de08fe3efd6093638a93119e112c2aea085acd925468c2` |
| `prepare_live09_revision04.py` | `7d0fb76cf4317e964df824bdecaaa53b49e141951c58d3301d7ff4e706e9d57f` |
| `prepare_activation09_revision04.py` | `0acb7aa748fb7100cb6ed2cbf6309228f343e41c666e93d192d38c3665c71cb7` |
| `collect_live09_revision04.py` | `d3d107db00ec1eb4505aff901aa0338347bb3b4405cff1b3b9f345f28339ae77` |

四份Python只做AST文本语法解析通过，未导入或执行。最初审查指出的handoff与实际物料互核、激活前旧公开清单自身pin，已在上述最新版本补齐；本报告不要求扩大业务或板端操作范围。
