# 第三批流程修补：编译与板B保存结果

2026-11-02发布`4e6c321f`。主程序261份对象、报告辅助程序另2份对象均完成ARM64交叉编译；完整链接、依赖检查及打包实际退出0。639项匹配源码解包后逐份SHA-256一致。[程序与源码恢复输入](../../../../07_backups/sn100_host/board_b/workflow_integration_20261102/integration_04/README.md)。

22:03:08核板B序列号`8314b1e8ffd0321f`及专用SSH主机指纹后，新包保存到`/home/rkshare/programs/sn100_shell/releases/4e6c321fba5147996246872587f0b429bb64f0fd060273b94c155ef3357a3135`。完整清单、主程序/报告程序指纹及实际动态库检查通过，保存脚本退出0。

**没有切换current、运行新程序或恢复主控。**current仍为`7c4abe30`；RK功能、真实仪器、原界面实屏、保存重开、响应/资源、锁争用和长期运行均待验。电脑只检查源码、交叉编译和打包，没有本机产品、匿名资料、模拟或回环测试。

## 本批改动及边界

八份窄补丁先在隔离副本拼接，再串行合入；639项正式输入与隔离副本逐字节一致，共26文件变动，没有用旧整文件覆盖其他成果。

- 优先使用当前瓶：保留收到消息时的连接代次，单孔回读先分流再校验，核一次许可、期限及三库版本；保存后等页面同版本才结束。无请求号时仍不能绝对识别未见过的同内容迟到包，当前42/48位差异继续阻断设备操作。
- 项目默认定标六项参数、原资料编辑权限：沿原UI与保存前备份，区分资料编辑和设备动作条件。
- 校准审核后应用：固定目标组原绑定前态，审核提交后重读、绑定事务内再核；失败保留已审核事实。其他较早的授权和已审核CAL新回包差异仍待处理。
- 启动登录及历史忙态：独立启动意图，等待真实业务页空闲；主动操作/取消后不反复弹，注销重登独立处理。
- 直接入口单实例：使用原多开开关，Linux范围为同一用户共享同一`/tmp`，与Windows登录会话范围不同；外部删锁及Qt平台先初始化的边界单列，尚未实际争用验证。

后续报告均值、主题、自动重连、LIS查询正文及N容量换算候选不在本包。LIS实际连接、耗材授权和探测未开放，曲线仍待计算。

## 核对入口

- [verification.json](verification.json)、[provenance.json](provenance.json)：版本、真实退出码、对象计数口径和未验范围。
- [build.log](build.log)、[build_exit.txt](build_exit.txt)、[dependency_report.json](dependency_report.json)：完整编译及依赖。旧`offline_closure_passed`仅为静态依赖工具输出，不是脱机测试。
- [build_inputs.tsv](build_inputs.tsv)、[source_snapshot.json](source_snapshot.json)：348项去重后的真实构建输入均进入639项源码清单。
- [merge_inputs.json](merge_inputs.json)：八补丁指纹、应用顺序、26文件及与隔离副本比对结果；`source_review/`保存对应固定交接和独立复核。
- [board_b_stage.log](board_b_stage.log)、[board_b_stage.exit](board_b_stage.exit)：独立目录保存与动态库原输出。
- [按真实可执行文件查主控](board_b_controller_exe_readonly.log)：主控会改进程标题，本次按`/proc`实际文件核到0个，5000无监听；程序文件仍为正确指纹，未启动或停止。
- [容量资料只读核对](board_b_capacity_files_readonly.log)：只在当前原资料及已知主控目录搜索，未找到600文件；不能推定全盘或压缩包不存在，也不据此否定原几何后备。
- `scripts/`为当次原脚本，路径绑定工作区，供查证而非直接安装使用。实际功能、视觉和资源结果继续记[同一当前卡](../../../../01_docs/plans/sn100_host_migration_plan.md#workflow-night-20261102)与[同一移植记录](../../../../02_records/20261022_sn100_host_migration.md#workflow-night-coordination-20261102)。
