# 板B SN500配置连接版本的恢复输入

本目录保存2026-11-02本轮通过编译和RK连接核对的上位机发布包、匹配源码、用户主题原文及组件/源码指纹，不包含患者、账户数据库或带认证字段的config.ini。板B序列号8314b1e8ffd0321f，SSH指纹SHA256:S4ihaw0V/Z8w7v3VlJOQEFLEH3Hp6nr3KjiUTxanwPg；不能套用到板A。

正式发布`eb9d9ae6d593fa20155f83f4e1c8455cc694da083c919f7142343187d1515e9b`，包SHA-256为81493f022b335e3f43751a6dc34fec0f7f60d38dd31facf00ee3bc9a8af902b4，主程序e235167c5691968928b5e18eaad0d43d08bad12d1c7638c59065f4111d2c5af4。压缩包按Git LFS规则保存，SHA256SUMS用于逐文件核对。matching_sources.tar.gz包含本轮源码快照中逐个核对过的构建输入及RK检查入口，source_snapshot.json记录正式编译输入。

恢复前核对板B身份，通过现有sn100.sh正常关闭上位机并确认退出；主控和实时窗口分别处理，不能误停或覆盖。发布包只解压到`/home/rkshare/programs/sn100_shell/releases/<发布号>/`，按原发布清单核对指纹、动态库和普通用户权限后切换current。不要在运行进程上覆盖可执行文件，不自动启动。

本次连接仍依赖已有完整原资料根`/home/rkshare/programs/sn100_shell/runtime/data`及其许可标记。original_project_root和instrument_configuration_file分别记录该根及config.ini绝对路径，theme_configuration_file记录同根theme_config.ini路径，指针为普通文件、lckfb所有、600权限。原config.ini的tcpmain/ip、tcplog/ip为127.0.0.1，主端口5000；其他值沿用本轮已核原工作配置，不从本归档补造认证资料。主题可用本目录逐字节恢复；写入前先核是否有用户后续修改，只备份受影响文件。

本轮保留了原资料聊天更新的日常sn100.sh，不用旧匿名启动器覆盖。原141份工作资料、29库的来源和有限配置恢复记录见[原资料归档](../original_files_20261102/README.md)。旧程序发布ef294a92仍在板B；只切回程序不代表原资料/新主题兼容已验，不能据此自动启动旧脱机业务。

本包不恢复主控、整机环境、串口或CAN；正确主控另见[主控归档](../aca_flow_test_20261102/correct_program_1735/README.md)。本轮RK专项17/0、实际退出0，正式程序启动/正常退出0与重开连接通过；主控48位与主题42位及归位报警、真实检测和长期稳定性仍待核。[实际证据与启动方法](../../../../04_references/sn100_host/board_b/20261102_sn500_connection/README.md)。
