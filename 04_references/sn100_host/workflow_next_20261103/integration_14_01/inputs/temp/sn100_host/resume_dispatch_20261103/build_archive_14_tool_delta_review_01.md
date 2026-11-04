# 第14批九个文件阶段工具差异复核

结论：九份候选逐字等于Root第13批冻结实物，只把`13_01`替换成`14_01`、`13-01`替换成`14-01`，没有其他代码变化或权限放宽。可供Root另存审定副本后继续准备实际输入；本报告只审工具代码差异，不批准第14批来源账、配置或执行，不证明编译、存包、RK软件或仪器通过。

本次直接读取`integration_13_root_frozen_01/`九份原件及第14批候选，不以作者的patch或旧候选代替实际比较。七份Python仅解析语法树；两份Shell只读逐字比较，没有运行bash、导入候选、执行patch、任何阶段、编译、网络、SSH、板端或显示操作。仅新增本报告及相邻九工具小清单，未修改正式源码、工具、旧13或固定归档。

## 实际清单与精确差异

`build_archive_candidate_14_01/candidate_manifest.json`实际3886字节，SHA-256为`d10e90e2c7c6c3bcb4babc8f422f8564591f805a1529c448c35e7a5d5697dd72`，`code_locked=true`。其中15份record（Root快照、九代码、README及四份静态材料）均按实际路径核对SHA和字节数。源码来源快照实际2810字节、SHA为`a1f6cea6a9f0cc3921f915dd3d726ab1306578c893ef9fbe7f5c5c5c3df79b8b`，九份来源也逐项等于该快照记录。

九份候选名称、导入、格式标记和输出路径全部采用相同替换。下表给出本次直接统计的原字节替换次数，所有行的候选字节均完整等于两次替换后的原件，未留下旧批次标记：

| 候选文件 | `13_01`次数 | `13-01`次数 | 实际字节数 |
| --- | ---: | ---: | ---: |
| `archive_integration_14_01.py` | 4 | 3 | 18122 |
| `build_integration_14_01.sh` | 5 | 0 | 4186 |
| `finish_integration_14_01_archive.py` | 2 | 3 | 7464 |
| `integration_14_01_object_binding.py` | 1 | 1 | 26233 |
| `integration_14_01_tool_support.py` | 22 | 6 | 33848 |
| `stage_board_b_release_14_01.py` | 8 | 3 | 23016 |
| `stage_board_b_release_14_01.sh` | 6 | 1 | 16775 |
| `verify_and_save_integration_14_01.py` | 6 | 3 | 21496 |
| `verify_integration_14_01_inputs.py` | 3 | 2 | 10108 |

作者`source_mapping.json`的九组来源和候选与本次直接读出的记录一致。`nine_tools.patch`、`validation_result.json`及`code_hashes.json`按清单核原件指纹，没有重演作者的静态命令或将其结果当作本次执行。相邻`build_archive_14_tool_delta_review_01.tools.json`固定九份候选实际路径、完整SHA和字节数，实际2043字节，SHA为`5b34d3dbd995d74159709209b5c43893a850c75239cc72a18b2f8063cb28e920`。

## 保持的文件操作边界

完整构建仍在普通WSL用户下先核冻结来源及`allow_full_build`，之后才创建全新`integration_14_01`工作目录；已有工作目录即拒绝。`build_integration_14_01.sh:64`清除对象复用环境变量，实际构建使用新`arm64_build`，先核已有匹配依赖，不以旧可执行文件替代失败编译，也不运行产品。

存包Python入口先读冻结配置及`allow_stage`，再核来源、实际构建产物及板B信任材料。已知ED25519指纹`SHA256:S4ihaw0V/Z8w7v3VlJOQEFLEH3Hp6nr3KjiUTxanwPg`、板B序列号`8314b1e8ffd0321f`、严格SSH主机检查和仅持有`immutable_store.lock`的方式均未改变。远端只另存到`releases/integration_14_01/<实际发布号>`；对current、主程序与原主控身份/指纹、七配置和三资料指针的前后保护保持原逻辑，不改current、不关闭/启动应用、原主控或服务，不切显示。`stage_board_b_release_14_01.sh:215–216`引入`rk_board.sh`只调用`verify_release`，未调用其生命周期函数。

成功尾部的删除仍只处理本次暂存依赖文件、本次空暂存父目录及本次上传包；失败留现场，不增加对旧13、旧归档、运行数据或用户临时目录的清理。相机停止是后来由用户安排的独立状态；九工具没有新增相机启停，也不能说原23字段文件保护包含了相机身份。

来源和数量保持动态：`tool_support`要求真实review、progress、来源脚本、底稿和最终全源表一致，源码数取完整实际字典长度，补丁顺序和每份原件受核；`verify_inputs`、归档和收证读取实际输入表、源码包成员及真实阶段退出。对象核对从冻结构建规则和实际输入推导source/moc/resource/helper集合，再核实际ELF和完整`.o`集合，main/helper/total按实际集合计算。名称替换没有写入本轮697源、对象数或通过结果。

## 本审范围之外的后续输入

候选manifest和README写“尚未收到Root最终源表、未写配置/Spec”是其准备时点的原记录，不因Root后来采用样本03而追改，也不据此否定后来接纳。Root另行提供的实际697后表`169f571193b2893e8cbb3df8359c723e414cff89232ccc6e5d3a9274c44022bd`及两源check/apply 0/0，须进入第14批新的真实来源账和独审；本报告不替代该核对。

本审没有采用患者B1或全业务审计的其他问题，没有把第13批未编译事实改写成已编，也没有给尚未就绪的第14批false配置/来源账提前放行。Root后续须核实际来源账、配置证据和逐项许可，另存九工具时核复制字节，再安排必要完整编译及独立存包。原C7留驻、旧关闭保护待处理和各项未验范围继续按Root的当前约束执行。
