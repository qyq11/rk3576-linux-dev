# 运行准备生成器02：权限修复及派生后像核对

2026-11-03。定向比较 `prepare_runtime_batch10_02.py` 与保留的01。02实际SHA为 `96dfd61e863b41a975cf3c5173648720111ecf2dfec388c187233858b6016c6e`；01仍为 `ee16a7cc5f45b3763cc5c87218cee72aebfd8bd9639d46d9ea0335455347db4f`。只做源码/字节、AST和已生成本地记录核对，没有运行任一生成器或派生脚本，没有联板、改正式或冻结代码。01原问题另留01报告。

## 定向结论

01的等待器权限阻点已在02源码关闭：唯一行为差异是chmod700改750，并将文件设为root:lckfb。父目录root:lckfb 750允许lckfb穿过；waiter组可读/执行且不可写，适合后续普通用户调用。三记录640和观察目录lckfb 700未改；没有新增应用启停或切current语句。

总控另预建lckfb拥有、700的captures父目录，由waiter创建不存在的capture_01叶目录，这一安排与等待器的独占创建要求相符。不能把capture_01也预建，不能将其放进观察结果目录；后续启动/等待脚本到齐时仍须核实际路径，当前没有板端目录权限通过结论。

其余门、冻结值、记录及prepare/activate边界与01报告相同。新意图全部动作许可为true表示总控本批范围，不表示它们已执行；实际prepare仍由强制动作和模板分支限制。未来旧停、激活、唯一新启动脚本尚不在本次审查范围。

## 已生成的本地后像

读取到 `runtime_batch10_01/` 已存在；输出批次继续用01是有意命名。以下只核本地文件，不推定生成器或板端操作的退出：

- `prepare_flat_batch10_01.sh` 原始字节精确等于 `set -- prepare-flat\n` 加 `transition_frozen_prepare_01.sh`，动作被固定。
- 意图为固定平铺JSON，frozen为bool true、supervise_seconds为整数0，观察ID `integration_next_02_lis_01` 符合CPP/等待器格式；观察目录在本批元数据下的独立observation子目录，不混入意图、缓存或工具。
- `root_prepare_values.json` 内三个记录SHA逐个与意图、批准说明、七配置清单原始字节一致；配置清单恰为七行。old_stop SHA仍是64零占位，只属于未取得旧停结果的准备状态。
- 安装派生脚本实际含waiter750及root:lckfb，三记录root:lckfb640、元数据父750、观察目录lckfb700；拒绝既有目标，使用noclobber和umask077，各记录落盘后重哈希，末尾重新核七配置。没有运行该脚本。

| 本地后像 | 实际SHA-256 |
| --- | --- |
| runtime_launch_intent.json | e8810370280fe56587bf15aa10f0c3e1377ed31c30b5eb307ef0d44bcdda40eb |
| root_approval.md | 20747493a79ab553d1c22e6d2d0ac9019a7f9f759d1c98be64201ca835bf6eb2 |
| configuration.sha256 | ad11ae68d8ed119a5a34ed65acf98dc2d15bafba6afaa950bee5fd7d3805981c |
| root_prepare_values.json | 381e03fad86cc9d5dbeb3da1284a929305d588a2bf1bd31924e4ebe8792ea7bb |
| transition_frozen_prepare_01.sh | b4737db9ab3c8b0c9db92fdeb0785536956488096ab45742423ca3b50ac9281b |
| install_runtime_metadata_batch10_01.sh | a01f46f24858f0ee095898bf4b38c86f3a96748405393bb28586e7ab53aa4af1 |

02只做AST解析，未import或调用其中函数，真实退出0；源码差异与后像指纹核对亦为只读。没有执行安装、prepare-flat、旧停、激活、启动或观察，不给这些阶段补0。后续实际launch/stop脚本仍须独立定向审查，并持续通过已知SSH公钥及序列号Rootguard，而非仅凭本报告或boot/current放行。
