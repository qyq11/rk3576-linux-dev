# O10-1打印机本地设置：ARM应用与匹配构建输入

该检查点在原用户设置子页3接入本地设备枚举、选择与`printer/name`保存；不提交打印、不更改系统默认设备。原353登录和348仪器检查点保持原位置，本目录为独立361输入版本。

| 文件 | 大小/校验与范围 |
| --- | --- |
| `sn100_shell_eace344340a0a12925cb0e760f9c226f0bfc1409db4b5a759e6c141003273b57.tar.gz` | 69,222,241字节；SHA256 `3d12ee827dd9c734944939e40aeb7ff23b15a57aaea7cf1d28f690f92e4ee2ff`；33普通文件/31内部SHA，ARM主辅、私有依赖/插件、字体/模板和匹配生命周期脚本 |
| `sn100_source_eace344340a0a12925cb0e760f9c226f0bfc1409db4b5a759e6c141003273b57.tar.gz` | 919,069字节；SHA256 `4548ee31f2ac95fbd0554ac1139a2ec71de1b3710f6d74ee813c41559036769f`；严格361成员，逐文件等于`source_snapshot.json` |
| `source_snapshot.json` | 最终构建/发布输入清单；SHA256 `119cfab52d54bd61d4fb765a7666a8dddf74d2e110baf1a4207eab1d68cef83f` |
| `arm64_verification.json`、`verification.json`、`SHA256SUMS` | 真实构建/闭包/打包退出、对象来源、归档复核及本目录文件校验 |

release ID为`eace344340a0a12925cb0e760f9c226f0bfc1409db4b5a759e6c141003273b57`；ARM主SHA `9fae2c17c36b0986413bb5a9a6364ea11af9c6d0fa2a4e63894ec0cc38eba6b1`、helper `e818a52f38208cc7b0cf14cb74fbca38edf3d7d9d6f4713e8ee8a576a492cbf0`。两个gzip已核Git LFS规则；本次未自动提交/推送。

恢复前先在独立目录核本目录SHA及包内清单，再依原部署流程预检与发布；本轮未上传、切换或运行RK。源码包仅覆盖该版冻结构建输入，宜解压到干净项目副本的`06_programs/sn100_host/`并与清单逐项核对；其余根工作流、测试和文档由对应项目版本提供，不覆盖当前未提交工作树或现有运行数据。应用与源码须成对恢复，不能把旧353包说成含新设置页。

不包含业务运行库、配置、密码/私钥或认证摘要，不代替多store运行数据备份。Windows原始主编译独立退出收据及外部头原prebuild哈希缺口如实保留在总索引，未编造全量预后证明。原Windows完整行为/活动主题、Windows GUI、RK显示/输入/实际打印后端及服务依赖、真实打印/仪器、资源预算/同板RT与长稳仍待验。

[总索引](../../../04_references/sn100_host/printer_settings_20261024/verification.json) · [同一记录](../../../02_records/20261022_sn100_host_migration.md#o10-printer-settings-20261024) · [当前卡](../../../01_docs/plans/sn100_host_migration_plan.md#o10-printer-settings-card)
