# 板B原资料接入的有限恢复输入

**2026-11-02 18:13:32后不再支持切回旧脱机资料。**用户已要求删除板B的`runtime/previous_test_data_20261102`并停止后续脱机测试；该目录已实际删除。下文恢复方式是删除前的历史安排，`restore_previous_test_data.sh`保留原件用于核对，不再作为可用恢复入口；配置和启动器的历史输入仍保留各自范围。[删除结果](../../../../04_references/sn100_host/board_b/20261102_remove_offline_data/README.md)。

2026-11-02，板B序列号`8314b1e8ffd0321f`。这里保存旧/新启动器、用户新提供的SN500主题INI、逐键修改清单和文件指纹元数据，支持核对本次改动。完整过程见[部署证据](../../../../04_references/sn100_host/board_b/20261102_original_files/README.md)。不包含账户、患者数据库或带认证字段的原`config.ini`；141份原资料的来源仍是本机只读参考原件，当前工作副本在板B。

本次未覆盖旧测试数据，而是将其整体移到板B`/home/rkshare/programs/sn100_shell/runtime/previous_test_data_20261102`。`restore_previous_test_data.sh`仅在已确认板B、主程序关闭、生命周期锁可取得且没有后续连接/主题指针时切回该目录，同时就地保留新原资料，不删除资料、不启动程序。它不是整机或整个应用版本回退；后续程序升级、主控配置和连接变更须单独核对后处理。

本次原资料中只有两份INI、七键变化：新SN500主题五键以及同板通信两个地址，详见`configuration_changes.json`。`theme_config.ini`与用户附件相同；`sn100.sh.before`/`sn100.sh.after`是原资料接入前后的日常启动入口。`effective_manifest.json`只含路径、大小和指纹，不含数据库记录或认证值。

`SHA256SUMS`核对本目录说明以外的文件。原测试数据仍在板B，因此本归档不单独提供数据恢复；该保留目录缺失时应停止，不自行补造账户或数据库。
