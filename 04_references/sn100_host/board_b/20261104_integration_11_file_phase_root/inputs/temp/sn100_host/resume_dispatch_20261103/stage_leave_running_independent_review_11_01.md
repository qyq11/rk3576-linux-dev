# 第11批独立存包：原程序留驻证据独立复核

2026-11-04。只读 `workflow_next_20261103/integration_11_01` 的started/complete、Root和板端日志、真实退出原件、前后运行身份与配置指纹，以及本轮冻结cfg和必要冻结存包脚本。没有重新连接板卡、执行phase/产品/SQL/模拟或编译，没有改正式源码、工具或运行程序；只新增本报告。

结论：第11批本段独立存包证据一致，三个实际脚本退出均为0；新版本另存，未切current、未启动新产品。持存储锁解包和提交区间的前后快照中，原C7主程序、原主控、启动身份、三份入口指针及七份配置指纹完全相同。结论仅限 `locked_unpack_and_commit` 的这段留驻核对，不扩大为长期稳定、界面功能、真实仪器、实体操作或资源门槛通过，也不代替随后归档阶段的完成结论。

## 实际阶段与板卡身份

`stage_started.json` 的complete=false和 `stage_complete.json` 的complete=true分别保留；后者精确绑定前者SHA，并继承其全部输入/身份字段，没有换包后另填结果。开始记录为2026-11-04 01:20:43.962510+08:00，封口为01:21:25.225554+08:00；这两个时间不当作精确锁持有时长或长稳测量。

读取的 `logs/root_stage_01.exit`、`upload.exit`、`board_b_stage.exit` 均为真实0；Root stderr为0字节。板端upload和stage原始标记各准确出现一次，完整记录与complete中的8项结果原件SHA/字节绑定均相符。本审没有重跑检查，也不把脚本退出0写成新应用退出0。

目标确为板B：serial `8314b1e8ffd0321f`，SSH `root@192.168.100.123`，别名 `rk3576-board-b`，已知ED25519指纹 `SHA256:S4ihaw0V/Z8w7v3VlJOQEFLEH3Hp6nr3KjiUTxanwPg`。本次独立读取专用known_hosts公钥文件，实际SHA等started的 `71cbf5fce3f4b60ffb120ca572ea8b7046913eaa109d901bbab365cb53198ab4`，指定别名恰有一把公钥，其指纹与冻结工具和记录一致。实际冻结工具强制StrictHostKeyChecking、指定别名/专用known_hosts、ED25519并禁止自动更新主机键；板端脚本在锁前及持锁后核硬件serial。此核对不是仅凭IP确认板卡，没有读取认证私钥。

## 原运行身份和配置未变

板端日志及Root stdout各有唯一before/after运行快照和配置指纹的BASE64原件。本次独立解码后与本地四份guard文件逐字节相同，日志中SHA标记也一致，未只相信complete的unchanged=true。

运行guard前后均为23个唯一字段、1433字节，完整原文相同。除互相比对外，也逐项核对了已知身份：

- 同一boot：`0eb94389-f120-42ad-9042-9c21d96a1481`。
- current始终为 `releases/c7cbf438a3a034b25ae22accfaf48bacf0717174ce85bef2203ce8db143cb06e`；主程序PID `669382`、start_ticks `2102645`、UID `1000`，exe来自该C7目录，实际exe SHA为 `50d3f3b510772e717e456cc4e3e910060a82a6635b9d914f033e79f9ed6f0258`。
- 原主控PID `5008`、start_ticks `118180`、UID `0`，exe `/userdata/shared/ACA_V1.0OffLineD-500`，SHA `ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff`，监听 `0.0.0.0:5000`。
- original_project_root、instrument_configuration_file、theme_configuration_file三个入口文件的固定路径和各自SHA均与原值及前后快照相同；CONFIGURATION_SHA256均为ad11冻结值。

七行配置指纹原件前后各775字节，逐字节等冻结文件 `04_references/sn100_host/board_b/20261103_integration_10_keep_running/observation/configuration.sha256`。独立SHA均为 `ad11ae68d8ed119a5a34ed65acf98dc2d15bafba6afaa950bee5fd7d3805981c`。下面保留核对的指纹行，指纹核对不冒称读取并审查了INI业务字段：

```text
58374e000baaa65c58bb4d3cf1bb84458b4a4de7348e4f54371f2e957fa4b963  /home/rkshare/programs/sn100_shell/runtime/data/config_Debug.ini
216f4ad918600ae0f99b2fa689228ab810fd71b43736a1b57d084f630d443374  /home/rkshare/programs/sn100_shell/runtime/data/config.ini
029605f35924de646b5402f1e377c93cb26e01952e0daa5fda300fe47f7feed7  /home/rkshare/programs/sn100_shell/runtime/data/theme_config.ini
8b62bdfd565a38dbae844806c1e1ca43f56ad0f93ad5a827118cf21e40cc74fe  /userdata/shared/config_linux.ini
967dfbbde3771201b4cc33b07e9e4f1e37e9359a737bf90bb66441d840822e25  /userdata/shared/consume.ini
9470414d5f95a6b965a5d89e7594fb73ea99fb0743bf2a97bd3bf818d103d86f  /userdata/shared/GL4A_config.cfg
1c6cbb1f065c8eaf0a7701613ea2209320fdd604fb56371e93bf25795aafe116  /userdata/shared/number.ini
```

冻结cfg实际SHA等started和complete绑定，其许可仅allow_full_build/allow_stage为true；expected_current、原应用/主控exe SHA及七行清单引用也一致。冻结脚本用 `/home/rkshare/programs/sn100_shell/immutable_store.lock` 排他锁约束本次解包/提交，前guard在解包前、后guard在版本另存提交后；不把这把合作工具锁解释成阻止所有外部写者或长期锁住应用。

## 新包另存，未激活或运行

新release为 `356fe5f88efe52226105f68ec3d6479f70d3f4ad12bcb967da790a180d22b6d5`，仅保存至：

`/home/rkshare/programs/sn100_shell/releases/integration_11_01/356fe5f88efe52226105f68ec3d6479f70d3f4ad12bcb967da790a180d22b6d5`

实际本地包流式重算SHA为 `4f90ea77c1c9eef9e94b7af92761cb6efeecb5a77724daabaefc2828539ea6f5`，与build_complete、started、upload及stage日志一致。板端另存后的主程序SHA `3248b4fe66aa0014ccf3890bf253a1ea5905352981f0e8c692ad628b0c372642`、报告辅助程序SHA `9a6091a9aee0e38e51a1963f323559091d6cb879412e6cb4f3a11c6550ab568b` 均与真实构建终态相符。

板端stage原日志明确 `ACTIVATED=0`、`PROGRAM_STARTED=0`，complete为activation_performed=false、program_started=false，current_before/after均为C7。DEPENDENCIES_BEGIN/END内是ldd动态库解析，未发现not found；这不是执行新SN100或报告产品的功能/显示检查，不证明新版本已在RK运行。没有新应用实际退出码或新仪器业务通过结论。

## 本次原件绑定

| 原件 | 实读SHA-256 |
| --- | --- |
| 冻结integration_11_01_tools_config.json | `051977b52473c907e3df8dba3245781432aa619b79cb0a9f4b22b2430b92186c` |
| stage_started.json | `31507b500b955ca5084a2669cc4c6346b7fb4fa04554ae41dddcb9d67a571128` |
| stage_complete.json | `cb3fbc24340a7c529a892d67130d5d773b26efec7b853bbac50415ad1ebef6f5` |
| logs/board_b_stage.log | `c1ca053556f855e961a8d29924c02d7fdc82df9cbd773a926cb944ba645874c0` |
| logs/root_stage_01.stdout.log | `4b744a7cf3de079036d6f3b85db9f19f93de45266c8e0f97588190c0a60fe5b8` |
| live_guard_before.txt / after.txt（同字节） | `6777480f45a69c4cc8da017d0edcf4bcea82f6168fd181e98a6c6ec95ebd9091` |
| configuration_guard_before.sha256 / after.sha256（同字节） | `ad11ae68d8ed119a5a34ed65acf98dc2d15bafba6afaa950bee5fd7d3805981c` |

结论只覆盖这次存包锁内原身份与配置保持；真实新版本功能、长期留驻、实体操作、资源和同板控制影响继续按各自授权及证据分别登记。本审没有启停任何程序。
