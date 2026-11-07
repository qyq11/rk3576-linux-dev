# RK3576 Linux 开发项目

基于立创泰山派 RK3576 的 Linux 开发工程，面向凝血分析仪，包含 SN100 Qt 上位机移植、实时内核、硬件接口和整机系统镜像。

## 项目内容与进展

| 方向 | 当前范围 | 详细入口 |
| --- | --- | --- |
| SN100上位机 | 部分页面和通信已有板A运行证据；完整仪器业务仍待验收 | [源码与构建](06_programs/sn100_host/README.md) · [移植计划](01_docs/plans/sn100_host_migration_plan.md) |
| 实时内核与接口 | 板A已有实时内核启动和短时延迟测量证据；接口按需接线验证，系统联调仍待完成 | [整体计划](01_docs/plans/development_plan.md) · [实时内核记录](02_records/20261022_preempt_rt.md) |
| 整机系统镜像 | 完整镜像已编译归档，烧录后的启动与服务待验证 | [系统工程](06_programs/rk3576_system/README.md) · [镜像与使用方法](07_backups/system_image/ubuntu24_server/README.md) |

**板卡分工：**板A用于Qt上位机及后续SN100测试，板B用于小车。两板配置和验证结果分开记录。

最新修复准备及待验条件见[当前移植任务](01_docs/plans/sn100_host_migration_plan.md#prepare-without-manual-20261105)。编译通过、RK软件运行通过和接真实仪器通过分别记录。

## 先看文件用途

| 想找什么 | 从这里进入 |
| --- | --- |
| 开发环境、板卡说明与运行入口 | [项目使用与资料指南](01_docs/knowledge/rk3576_project_guide.md) |
| 学习资料与开发计划 | [文档目录](01_docs/README.md) |
| 实际操作、失败与验证结果 | [操作记录](02_records/README.md) |
| 界面截图与现场照片 | [照片目录](03_photos/README.md) |
| 板级资料、日志与验证证据 | [资料目录](04_references/README.md) |
| 安装工具与独立接口实验 | [工具目录](05_tools/README.md) |
| 正式程序源码与构建脚本 | [程序目录](06_programs/README.md) |
| 安装、恢复与回退文件 | [备份目录](07_backups/README.md) |

详细进度和操作过程更新到对应计划、记录；首页保留项目展示和导航。开发约定见[AGENTS.md](AGENTS.md)，AI使用的项目流程见[.agents/](.agents/README.md)。

## 获取项目

仓库：[qyq11/rk3576-linux-dev](https://github.com/qyq11/rk3576-linux-dev)。安装Git和Git LFS（大文件管理工具）后执行：

```powershell
git lfs install
git clone https://github.com/qyq11/rk3576-linux-dev.git
git -C rk3576-linux-dev lfs pull
```

源码、记录、图片、证据和必要备份随Git保存；本地参考工程、工具安装包及临时构建文件的归属见[项目指南](01_docs/knowledge/rk3576_project_guide.md#文件放在哪里)。
