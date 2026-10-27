# 2026-10-27 当前统一主程序的RK首次部署包

来源为正式`06_programs/sn100_host/`当前518项输入，与此前`native_03/final_before.json`输入字典逐项相同。本轮在WSL Ubuntu-24.04/qyq全量交叉编译主程序及报告辅助程序，ARM64依赖检查、上传SHA和板端实际动态库检查通过。源码未改写，原参考工程及生产配置未使用。

- 发布ID：`e230d2771c614ccc6f0a9ba4fe345cc632e7ddc1aaa56630a4a8f0036c5499d1`。
- 主程序SHA-256：`cd6d2ed9d9d3639a2f3af257101036fbe9fc3068a735a2fff3ecdca671ce9a63`。
- 报告辅助程序SHA-256：`520519de8cf29ef460b441189db8a9b90ec5acc4a9980ac9d807e74ceeb8092e`。
- `sn100_shell_<发布ID>.tar.gz`：实际上传的不可变应用包，含本轮程序、私有运行库/插件、中文字体、许可及目标生命周期脚本。
- `sn100_shell_sources.tar.gz`：与发布输入匹配的518项源码/构建/资源及必要匿名夹具；每项已从归档内读回核SHA。
- `source_snapshot.json`、`release.json`、`release.SHA256SUMS`、`dependency_report.json`与外层`SHA256SUMS`分别保留输入、内容、依赖及归档校验依据。压缩包按现有Git LFS规则管理，未自动提交或推送。

恢复范围为本版本应用及匹配源码。系统内核、Qt Core/Gui/Widgets、EGLFS、图形驱动和输入环境沿用已有板端及`07_backups/qt_environment/`记录，本包不等于整机镜像。业务数据库、测试密码、私钥及认证材料不在包内，也不把源码恢复包当一致业务数据备份。

已部署至`/home/rkshare/programs/sn100_shell/releases/<发布ID>/`，`current`指向该版本。首次部署未覆盖已有SN100数据，不另做整机备份。复用同一版时，启动、状态、正常停止和取证无需重新编译；在WSL项目根执行：

```bash
SN100_RK_WORK_DIR="$PWD/temp/sn100_host/rk_board_20261027_01/release_work" bash 06_programs/sn100_host/shell/rk.sh status
SN100_RK_WORK_DIR="$PWD/temp/sn100_host/rk_board_20261027_01/release_work" bash 06_programs/sn100_host/shell/rk.sh stop
SN100_RK_WORK_DIR="$PWD/temp/sn100_host/rk_board_20261027_01/release_work" bash 06_programs/sn100_host/shell/rk.sh start 0 offlineAdmin
```

当前RK已经按用户要求重启，程序保持关闭，不配置开机自启动；现场用户此前确认显示与鼠标可用。新增独立命令入口rk_control.sh已同SHA安装为板端sn100.sh（不改变包内程序），start/status/stop/status实际通过。主界面数据根为`/home/rkshare/programs/sn100_shell/runtime/data`，空白原值结果/原报文辅助入口另以显式参数在隔离数据根验证启动及重开。两种模式不混用同一个结果库。

实际通过范围及未验项见[同一移植记录](../../../02_records/20261022_sn100_host_migration.md#rk-board-first-deployment-20261027)和[本轮证据](../../../04_references/sn100_host/rk_board_20261027_01/verification.json)。逐页增改删查/保存重开、键盘及中文输入、原Windows视觉对照、真实仪器/LIS/实体打印、认可资源预算、同板控制和长稳仍待验。

手动使用（在RK终端或SSH登录后执行，不加sudo）：

```bash
cd /home/rkshare/programs/sn100_shell
bash sn100.sh start
bash sn100.sh stop
bash sn100.sh status
```

每次按需选择一个动作。界面关闭图标在本发布仍禁用，已登记后续修复；当前用stop正常退出。rk_control.sh为本次附带的外层命令入口，校验现有发布后调用原生命周期，不配置自启动，也不执行系统或仪器关机。包内release.SHA256SUMS范围不变。
