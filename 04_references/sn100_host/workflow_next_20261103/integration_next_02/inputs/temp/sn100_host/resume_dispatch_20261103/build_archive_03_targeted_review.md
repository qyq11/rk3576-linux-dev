# 构建归档候选03：关键修点定向审查

读取时点为2026-11-03 20:46:55 +08:00。对象为 `workflow_next_20261103/build_archive_candidate_03/`，对照上一轮 `build_archive_next_independent_review.md` 和candidate02实际字节。此时尚无 `handoff.md`，因此只审所列字节，不作终版晋级或动作许可。没有改作者或正式文件，没有执行阶段、构建、产品、网络、板卡、服务或显示操作。

## 结论

02提出的锁内图形/服务/显示复查缺口，以及主控只按精确路径计数、只认至少一条5000监听的弱检查，均已在03所审代码中补齐。未见为绕检查停止或杀旧应用/主控的新增路径；默认未冻结及false/空值门保持拒绝。

**末标记尚未按“归03”的字面要求修改。** 实际stage Python第350行打印 `BOARD_B_INTEGRATION_NEXT_02_STAGED=1`，从02目录的旧误标01修成了02。现行WORK、远端新目录和transition审计命名空间也仍是 `integration_next_02/transition_next_02`。若03只代表工具修订号、实际执行批次仍为02，应由总控在最终交接中明确区分；不能在结果中写成末标记已经归03。本报告不擅自要求作者整套改名，也不把这一命名差异冒充板端失败。

## 两项关键检查

`transition_release_next.sh.in:50–51` 先持 `immutable_store.lock`。activate路径在`:260` 做一次quiet检查，`:262–263` 再持 `lifecycle.lock`，`:264` 立即重做同一完整quiet检查。它重新枚举GUI/报告/检查进程，含已删除exe名称；重新核 `sn100-shell.service` 仅inactive/failed，重新核VT7和DRM既无持有者也无读取异常。存活用户进程exe不可访问会拒绝，不再凭读不到就当不存在。

锁后继续重核current、两个发布目录、停止凭据、批准记录、启动意图、配置清单和主控身份。在建立只属于本次的审计目录、临时current链接后，`:286` 再核旧current，`:287` 在原子切换前再次执行完整quiet检查。检查失败只退出；不会stop服务、发TERM/KILL、释放他人显示或回滚/强行替换current。模板仍将旧应用停止与唯一新启动留给总控。

新 `transition_controller_pid()`（`:97–114`）按 `ACA_V1.0OffLineD-500` basename计数，其他路径同名和deleted也计入，缺失或多份均拒绝。`transition_process_exe()` 仅对实际消失进程或经两次stat确认的死亡/内核线程跳过；身份不可访问或不能明确分类返回拒绝。唯一PID随后还必须匹配原真实exe路径和总控给定SHA，所以唯一但deleted或路径不对也不能通过。

`check_controller()` 第130–154行保存boot、PID、start_ticks、有效UID、实际exe、SHA和监听端点；逐条5000监听提取全部owner并要求其唯一值恰是该PID。空监听、缺owner或混入其他PID都不能通过。函数末再次核同名唯一PID、启动时刻、UID、exe、SHA和boot。锁后的完整身份须与锁前身份完全一致。没有重启原主控来获得这份结果的路径。

上述是静态调用位置与失败传播核对，不是实际板卡已安静、主控监听已通过或版本已切换的证据。

## 原九工具相对02的实际差异

本轮逐字节比较九份工具：八份完全相同；仅 `stage_board_b_release_next.py` 的最后打印字符串由01改02。`integration_next_tools_config.json`、`runtime_launch_intent.template.json` 也逐字节等02，没有新增源码/对象复用、业务关闭、产品启动或许可绕过逻辑。原完整重编译、对象绑定及只保存阶段的结论沿上一轮所列相同字节适用，不重审全文。

cfg仍 `frozen=false`、完整构建和stage许可false，实际源表、对象/构建和current等输入仍为空。启动意图仍未冻结、四项后授动作许可false、实际发布与路径为空。transition第6–7行直接拒绝未冻结模板，所有后续操作都在该门之后。不能把false模板或空值当作总控已冻结的f741源表及实际操作许可；Root还需另行绑定新实际源表、工具指纹及真实范围。

## 本次真实静态检查与指纹

只对改过末标记的Python做 `ast.parse`，未import候选，真实退出0；只对改过检查的transition执行 `bash -n`，真实退出0。WSL启动有环境代理提示，Bash未报语法错误。没有执行其正文、默认拒绝分支或任何阶段；本批构建、上传、stage、切换、启动、观察和应用退出均未知，不补0。

| 所审文件 | 实际SHA-256 |
| --- | --- |
| archive_integration_next.py | b127a25b65d3d223f09273c113a4487b065d8a1b6412bbdbf4b33b63752fc6e4 |
| build_integration_next.sh | 151ec4bb75936d608f80f27dc606a610067b32a6aa4cd1ec0e4bb862996cfa7b |
| finish_integration_next_archive.py | acaccfd0661cb7f90bf8eb64c997108f9cd9e412dd0af38d823639300bc4bbc2 |
| integration_next_object_binding.py | 322e24feb600903de6ea35d791bdf85aba064206e9e234bb33c2b758eb3e0497 |
| integration_next_tool_support.py | ad2cdcdd3e04150a246984f11656f4d1303f5d1234d87cdcac6e9398d3045855 |
| stage_board_b_release_next.py | 1f32af3de15b13f4f4d1fc2a6d1b4e7a2f47856c3a90e77009b896c153af35ab |
| stage_board_b_release_next.sh | 8f130d868bc6e8e2ad4d33abad560f117df18aaae6076362d89e081d75bdf8ef |
| verify_and_save_integration_next.py | b35591f3d1e96cae5b686cb3b3e7f6bee7e28d975b5c1d34c523cd5e4a233928 |
| verify_integration_next_inputs.py | 26d43ce36eb6f39b4ca91de2e24adb72d0c2ad3751a4dd1502e6331d5065fdec |
| transition_release_next.sh.in | f4ca11e27ea16ef2a5f65e894a180d62b58081b253008e6376ab015c41dd72ea |
| integration_next_tools_config.json | a0d17377e1636081f31ebe1ff94a31d76b7926e14e4a42f8288dd8aa0309c99c |
| runtime_launch_intent.template.json | 9e7a226d2960474026e781d74c651fc049519372e066fe29932d987a18ca5d16 |
| operation_contract.md | 95f24fdcc634be8049036dfee35e1aa6d933d0992be642fa54766474b7e653d7 |
| root_decision_alignment.md | 3139b571aace6635c47fe717ed686a37a6e08df415babf3923fa14ea28545a4e |
| README.md | 8f023768bc18236171583400310573a68b239a9754dd557caada48403787f6c3 |
| basis_inventory.json | 480cfe83e583e923d8840a88c18768ce922754bc87106ace710dee1563058ddd |

最终handoff到齐后，总控须核这些源码、cfg及意图仍为上述字节，并明确修订号03与执行批次02的关系；有变化按实际diff再窄审。本报告不批准现在执行模板，也没有把工具目录存在当作完整编译或RK结果。
