# 第09轮板端检查工具：第一次独立源码审查

2026-11-03，只读审查`temp/sn100_host/board_b/20261103_status_login_09_preparation_01/`。作者仍在收尾，本记录绑定本次读到的实际代码，不把尚未生成的发布号、对象数量、合同数量或运行结果预写成成功。没有修改候选/正式文件，没有执行构建工具、产品、测试或板卡操作。

**当前不建议冻结执行：先修下列两处确定核对问题，并补原告警RX独立解码。**不涉及新增产品业务能力。修后由总控复核实际工具指纹，再进入真实09对象绑定和运行。

## 确定问题

1. **允许的一次Qk会被汇总器第二道白名单误判。**`summarize_run.py`约第96行组装完整命令后仍是`allowed = {'Qj', 'Ur', 'Up', 'UP'} if mode == 'manual' and manual_sync_authorized else {'Qj'}`。前一道frame白名单虽已允许Qj/Qk，这里会把auto的Qk记成预期外命令并令软件结果失败。`prepare_tools_09.py`生成器未同步这段。应让auto仅允许Qj/Qk，人工模式保留原授权范围；再由已有的一次上限和BEGIN/END区间核查约束Qk，不能全局放宽任意发送。
2. **具备完整可比证据的页面不符被写成pending。**`current_alarm_observer_09.h`约第365行只把多发、异常TX、QK结束Qk等待和displayRejected纳入contradiction。当真实Qk已出现、连接不变、证据完整、没有Ql/QL混入且3秒等待后`pageMatch=false`时，`page_correspondence_evaluated=true`但总体`evaluated=false/status=pending`，不会加失败检查。应把这类已经可以比较的实际不符单列fail；连接改变、证据容量不足或自然新事件混入等不可比较条件继续待验，未收到Qk也不冒称设备故障。

## 验证链缺口

`current_alarm_observer_09.h::pageMatchesObservations()`目前把页面行与产品`OriginalCurrentAlarmState`已转换的observation比较。`actual_payloads`保存了真实payload_hex，但没有独立解码Qk二字段/QK三字段后核转换内容；`collect_case_09.py`只调用`summarize_run.py`，后者也没有该解码段。因此它只能证明“产品观察值→当前表”，还不能作为“原RX→转换→当前表”的独立证据。应在事后只读解析真实RX/捕获的原payload，并与同连接、明确Qk区间的观察和表格核对；不生成包、不建模拟服务器。

上述问题已经分别发给总控，总控已派原作者修正。待修后的源码应另记指纹与结论，不改本次审查所指的原事实。

## 已核可保留的边界

- 相对08固定检查器，本次源码的旧`check()`标签没有删除，仅新增LIS软件状态和显式Qk检查两项。源码调用计数26→28不是实际运行检查数，运行仍按本次数组统计；原24基础和急诊可选分支保持。
- 急诊观察继续使用实际原INI指纹、干净当前快照，明确`new_construction_proven=false`、`same_main_memory_bool_read=false`。runner只对本次公开摘要清单`chgrp lckfb; chmod 640; runuser test -r`，没有扩大业务INI权限。
- 原基础段结束后才调用`CurrentAlarmObserver09::run(shell)`。观察器只通过原顶栏按钮的实际控件事件进入查询槽，不直接构造或注入QB/TR/N/Qk/QK/告警，不点击开始、暂停或复位。最多一次Qk，QK仅记当前异常并核等待仍在，不作为Qk确认。现TX原块和本地区间都保存，完整日志另由事后解析器核。
- 页面通过原`btnClose`请求关闭，等待`viewClosed`及宿主不再打开，不用hide/accept绕开SQL/动画；关闭不完整保留结果，外层仍有真实时限。`finishReview`等原主窗关闭守卫，检查器结果、应用等待码、封装码和case码分别保存。
- LIS观察对象从`qApp`直接子对象查找，不新开通信；只读已提交设置、真实状态和状态/错误事件，不记录LIS正文，不调用open/send/reload/save/stop，也没有服务器构造。没有真实对端保持pending；软件状态可读和真实通信通过分开。
- runner使用flat路径`$base/releases/$release_id`核current和应用SHA，不套旧嵌套目录。板B序列、原主控exe完整SHA、唯一主控、5000监听及生命周期锁均在启动前核，不自行起主控。
- 冻结/构建驱动要求实际09完整构建0、官方源码包、匹配source表/依赖/完整对象；保留真实main，另编检查器观察CPP后链接同批全部主对象。对象数量和发布号来自实际清单，静态observation_scope中的null没有回填。这里核的是工具代码，不是实际绑定已执行。
- 关闭合同从最终冻结main与runner生成，原Task/N保护仍分别绑源码，不把空闲READY说成业务或在途一次退出。新增`LIS_CONNECTION_STOPPED`和`LIS_CONNECTION_SHUTDOWN_UNCONFIRMED`；告警页打开时条件要求`ALARM_HISTORY_VIEW_CLOSED`，原19项不能直接预填为本次最终数量。

## 启动前的LIS范围守卫

候选runner本身只核tcpmain/login键，LIS观察器在自动连接发生后才核127.0.0.1:3800等值。总控已明确最终单次派发外层会先核本轮已确认的08权限复核两份摘要清单本身SHA，再对3INI+4主控配置逐份`sha256sum -c`，同一次SSH内立即调用09 runner。此方案可覆盖启动前范围保护，但执行前必须实核该外层脚本与所引用固定清单；不能用启动后snapshot代替。16:09:45只读检查无3800监听仅属于该时点，不当作之后永远没有对端。

## 总控可执行的真实链

1. 作者修点后，核最终工具SHA、源范围与handoff；handoff当前开头仍是“最小准备”旧时点，不用它代替实际文件和最终构建交接。
2. 从本次正式09的`build_complete.json`和官方归档取真实release、源码包绝对路径与SHA，普通WSL用户运行`execute_final_09_build.py --execute --integration-directory <本次integration_09绝对路径> --release-id <实际09> --source-archive <官方源码包> --source-archive-sha256 <实际SHA>`。该驱动串行冻结、链接同版检查器、核静态依赖，并保存真实每阶段退出码。
3. 只有驱动实际0、`execution_09_result.json`显示checker_built=true且最终handoff绑定正确，才取其八份上传物料、真实应用/检查器SHA和最终关闭合同。保留旧08及任何失败目录，不覆盖。
4. 总控串行核板B固定SSH指纹、序列、current、工具和配置摘要清单/文件SHA、原主控活体及许可范围，在新09独立verification目录调用runner，attempt必须新建；不重起主控、不切其他聊天的显示。
5. runner结束后取真实四类退出和原始日志。用`collect_case_09.py --execute --remote-case <本轮真实已结束路径> --expected-dispatch-exit <真实值> --expected-case-exit <真实值>`只读采集，保留远端前后SHA与白名单原件/脱敏差异。
6. 单独看`parser.exit`及新增原RX独立核对结果。collector退出0只说明采集链完成，不能覆盖解析失败、基础检查失败、可选pending或软件退出失败。分别登记基础检查、可选项、LIS软件状态/真实对端、Qk实际返回、正常关闭、实体操作及资源范围。

本次没有运行任何上述步骤，没有通过结论。
