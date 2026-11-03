# 第二批流程整合：编译与板B保存结果

2026-11-02发布`d71e4cf4`：261个ARM64对象、主程序及报告辅助程序完整编译/链接、依赖检查、打包实际退出0。639项源码恢复输入解包后逐份SHA-256核对通过。[匹配程序与源码](../../../../07_backups/sn100_host/board_b/workflow_integration_20261102/integration_03/README.md)。

板B序列号`8314b1e8ffd0321f`和专用SSH主机指纹已核。发布已放到`/home/rkshare/programs/sn100_shell/releases/d71e4cf4db31b7c59cbbf217d2f80c81fbdb42cd137e7cc3f274e14043683108`；完整包内清单、主程序/报告程序指纹和实际动态库核对通过，板端保存脚本退出0。

**没有切换current、运行新程序或恢复主控。**current仍为`7c4abe30`；主控5000无监听。RK功能、真实仪器、原界面实屏、保存重开、响应/资源、长期运行和同板控制影响均待验。电脑只检查源码、交叉编译和打包，未做本机产品、匿名资料、模拟或回环测试。

本批范围为质控/定标任务身份及原始结果归属、Qq通道读数、试剂剩余次数/耗材界面、LIS TCP基础。曲线未计算，试剂设备动作、耗材真实授权和真实LIS连接未开放；后续优先使用、项目默认参数、校准权限及启动登录修补不在本包。

## 核对入口

- [verification.json](verification.json)：版本、实际退出码与已验/未验范围。
- [build.log](build.log)、[build_exit.txt](build_exit.txt)、[dependency_report.json](dependency_report.json)：完整构建和静态依赖。旧工具的`offline_closure_passed`只是依赖检查输出，不代表运行了脱机测试。
- [build_inputs.tsv](build_inputs.tsv)：160项源码、127项头、48项表单、11项资源和18项报告辅助来源，类别有重叠，去重后348项全部被本批源码指纹覆盖。
- [merge_inputs.json](merge_inputs.json)、[source_snapshot.json](source_snapshot.json)、[provenance.json](provenance.json)：六份实际合入补丁、本批639项输入及程序/包指纹。新LIS目录已进入源码清单。
- [board_b_stage.log](board_b_stage.log)、[board_b_stage.exit](board_b_stage.exit)：板B保存、完整清单和动态库原输出。
- [board_b_controller_exit_readonly.log](board_b_controller_exit_readonly.log)：21:29:56只读状态和19:45至20:10内核异常关键词检查，匹配0行；未找到线索不等于退出原因已排除或已确定。
- `source_review/`保存各固定候选交接、指纹和独立审查；对象编译及源码复核不能代替RK或仪器验收。
- `scripts/`保存当次原脚本供查证，路径绑定原工作区，不作为可直接复用的安装命令；正式构建入口仍是SN100的`shell/build.sh`。

全部后续任务维护在[同一当前卡](../../../../01_docs/plans/sn100_host_migration_plan.md#workflow-night-20261102)，实际过程追加[同一移植记录](../../../../02_records/20261022_sn100_host_migration.md#workflow-night-coordination-20261102)。
