# 仪器操作离线检查点（2026-10-24）

本归档固定“原仪器页面/人工维护 + 已验O09-2只读状态”检查点。之后正式工作目录继续开发的密码登录等功能不在此包内，不能把本包当作后续最新整机版本。

- ARM包：`sn100_shell_00414bde726a3dd1c2460caf413c7240f56b2b907bdee512962d989c201b7354.tar.gz`（68,077,183字节）。主程序、报告helper、应用私有库/插件、字体/许可、启动封装及源指纹一并保存；33普通文件、7目录、31内部SHA条目已独立核验。
- 源码：`instrument_checkpoint_source_20261024.tar.gz`，468项白名单源码/构建/UI/资源/匿名夹具，顶层为`06_programs/sn100_host/`；额外根清单`source_sha256.json`不污染程序源码目录。压缩包已读回核全部内容与安全路径。
- `compiled_inputs.json`为实际统一程序348项冻结输入，与部署包`release.json`完全一致；`arm64_verification.json`保留真实build/check/package退出、架构、源码稳定和外层driver CR错误的区别。

## 已验范围

WSL Release仪器专项两QPA各10/0、存储11/0及适用累计通过；Windows32主/helper构建与实际依赖核验通过；ARM64主/helper实际构建0、56 ELF离线闭包errors=[]、打包及全部内部SHA通过。仪器动作未启用，没有运行目标ELF、部署、安装、烧录或连接仪器。

原Windows活动主题/完整行为、RK显示/输入/真实依赖、实际仪器协议与动作、资源预算/同板控制及长稳仍待验。WSL资源结果见[本轮证据](../../../04_references/sn100_host/instrument_20261024/resource/verification.json)，不折算RK达标。

## 恢复范围与使用

先核本目录`SHA256SUMS`，再核应用包内`release_id`及`SHA256SUMS`。应用包只恢复该检查点应用及其私有运行闭包，不恢复/覆盖Linux、RT内核、引导、GPU/输入驱动或仪器固件；板端仍依赖现有匹配Qt/EGLFS/KMS/输入环境，须按[RK首验卡](../../../01_docs/plans/sn100_host_migration_plan.md#rk-offline-card)核实。

源码只解压到新的隔离恢复目录，保留本仓库现有正式源码和后续未提交修改；不要直接覆盖正在开发的`06_programs/sn100_host/`。主入口仍为`shell/shell.pro`、ARM脚本`shell/build.sh <绝对输出文件>`；需要匹配Qt5.15.13目标sysroot及host uic/moc/rcc、C++14/aarch64-linux-gnu-g++。已有正式依赖输入沿用`07_backups/qt_environment/`的Qt/SQL/报告/QtSvg归档及其SHA清单，不以temp中的唯一副本作为恢复来源。

本包默认Guest，启动功能为`--accounts --history --catalog --samples --instrument`；回放和自建本机回环需要显式参数，均不连接真实仪器。恢复后使用新的绝对离线数据根；原有运行数据库/维护记录与配置均未包含，也不会由归档自动覆盖。将来部署此版本时应明确指定本包及目标目录/数据范围，不能运行更新后的正式源码prepare再宣称仍是本包。

未归档数据库、生产配置、患者数据、bootstrap秘密、密码/私钥或认证凭据。两个压缩包按现有Git LFS规则保存，不自动提交/推送。完整过程见[同一记录](../../../02_records/20261022_sn100_host_migration.md#instrument-offline-result-20261024)。
