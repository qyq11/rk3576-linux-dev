# 最终06归档与专项检查输入的独立复核

本次只读复核官方归档、实际构建输入和总协调已生成的专项输入、构建结果记录。我没有执行freeze、bind或build工具，没有重新构建、联网、操作板卡，也没有修改已冻结文件。下文分别记录文件一致性和总协调实际执行留下的结果；均不构成RK运行通过结论。

**已核实：官方源码为649项，主程序实际对象为267个；真实`main`保留在`obj_0.o`。**数量从官方snapshot、源码包成员、实际`build_inputs.tsv`和对象目录分别读取，没有沿用旧批次数量。

## 正式发布与官方归档

官方目录：`07_backups/sn100_host/board_b/workflow_integration_20261102/integration_06/`。

| 项目 | 实际值 |
| --- | --- |
| 发布编号 | `14ce9714aaa0017c7ad78ff3f551d978672e5def79b96e817605295d7e59020c` |
| 主程序SHA-256 | `f3e814b4110b14ddbaef8c4516ab8a5b1c43747c45dcd4fff54ad3af0806ec6c` |
| 源码包SHA-256 | `c1a21586e8b9a1adaaefd9ac1743f8b0f74c04a45f37001450de61b42402369b` |
| 官方源码snapshot文件SHA-256 | `48c4010b1e4b74fc3ff0664cb16d6981c2e6c7fe117aaaf54cf39790ae3bb213` |
| 官方来源说明SHA-256 | `2f890f8b25c9b1fd4eff4f64de030e0e85cb6ca6680d7892bfe5611f70ebff17` |
| 官方`SHA256SUMS`文件SHA-256 | `a1ea97096a579929fbdd2eed63f6a6c17b004c815e642902ee520af20abdde86` |

逐一读取并计算官方`SHA256SUMS`列出的7份文件，7份均匹配。只读列出源码tar，恰好649个成员，包含`06_programs/sn100_host/shell/main.cpp`，未解压。官方snapshot字典也恰好649项，与provenance及`build_complete.json`所写数量一致。

实际`integration_06/build_exit.txt`为0。`packages/latest_release`指向本批stage；stage的清单SHA和`release_id`均等于上述发布编号，stage主程序SHA等于官方主程序SHA。stage的`release.json.sources`也是649项，与官方snapshot逐键比较无差异。

## 267个对象和真实入口

| 实际输入/产物 | 数量与核对结果 |
| --- | --- |
| 普通source | 164项，专项绑定记录中的相对路径及顺序与实际TSV一致。 |
| header | 131项，其中92项含`Q_OBJECT`；根据冻结头文件及TSV顺序重新核对，92个moc的来源和生成名一致。 |
| resource | 11项，按冻结`build.sh`资源名称顺序重新核对，11个qrc的来源和生成名一致。 |
| 主程序对象 | `164 + 92 + 11 = 267`，实际目录为连续`obj_0.o`至`obj_266.o`，没有缺项。 |
| 对象文件指纹 | 独立读取267个实际对象并计算SHA，与`link_provenance.json`的267项逐个比较，差异0项。 |
| 生成文件指纹 | 独立读取绑定记录中的152个moc/qrc/ui生成文件并计算SHA，差异0项。 |

真实`source_program`为本项目正式`06_programs/sn100_host`，不是05或参考工程。`shell/main.cpp`在普通source的索引0，对应`obj_0.o`；专项记录`main_object_kept=true`且`main_object.input= shell/main.cpp`、`main_object.index=0`。直接读取对象头确认`obj_0.o`是ELF64、小端、可重定位对象，机器类型为AArch64（183）。

- main源码SHA：`a0683da0485f71ac2b8af5abda6c7251fc35b65cc19288dd63c9d4ea1193bed7`。
- main对象SHA：`6121b38519c202b59cb378409ebeb1ebd81e1167828c661e8205d826769864ae`。
- 本次已生成的链接记录自标识：`fbf71faedb7cd39117f094e04c7c092f01649f05b6fa527b34312edff67c1fa0`；该值按记录原文引用，本文没有声称重新运行绑定工具。

## 专项prepared与冻结输入

总协调执行后，`prepared_inputs.json`已写为`bound_to_final_06_archive`，发布编号、官方源码路径、来源说明SHA、官方清单SHA和主程序身份均与上述正式输入相符。prepared中的`object_count=null`是冻结阶段尚未生成对象绑定的设计；已生成的`link_provenance.json`和`review_build_provenance.json`实际都记录267个正式对象，没有把null当作数量通过。

专项`frozen_06/sn100_host`实际649份文件；逐一计算SHA与官方snapshot比较，差异0项。专项`source_snapshot_06.json`与官方snapshot的649项内容完全相同。两份JSON字节SHA不同是重新排序/序列化所致，专项字节SHA也与prepared中绑定值相符，不能只比较JSON文件字节就判断源码变化。

进一步核对结果：

- prepared的9份模板SHA与当前实际模板文件全部一致。
- prepared的4份退出合同文件SHA与当前实际文件全部一致。
- 链接记录的`formal_identity`、模板绑定和合同绑定与prepared逐项一致，链接记录中的prepared SHA也等于实际prepared文件SHA。
- 267项对象记录的索引和路径逐项对应本批`integration_06/arm64_build/obj_N.o`，普通source、moc和resource顺序均无差异。
- 已生成的专项构建来源说明指向同一链接记录，并写`formal_object_count=267`。
- 退出合同的发布编号和main SHA与本批相符，要求`ORIGINAL_CHANNEL_WORKER_STOPPED true`；没有凭空要求`CHANNEL_STORE_CLOSED`或`ORIGINAL_CHANNEL_STORE_CLOSED`。

## AD接口是否影响专项检查

只读比较最终正式文件与此前审过的06候选：`InstrumentPage::receiveOriginalAd`签名和函数体，以及`read_only_messages.cpp::parseChannels`函数体，在仅归一CRLF换行后逐字一致。页面头文件、页面源码、业务只读消息解析、帧解析和`DeviceControl`当前正式文件的SHA也都匹配最终官方snapshot。

因此目前没有发现会使专项AD观察器接口失配的变化：仍按当前连接代次接收Qq；只消费声明数量前N对，重复完整键取首值，额外尾字段不强行加入通道表；普通空格拆分保留空项，显示前四列，并用`Qt::UserRole`标明是否为本次上报。短列表未提供列按原版保留旧文字但reported=false；断开或换代清为`-`和false。没有为专项检查改通道行、405/450表头或角色规则。

## 总协调实际执行后的补核

总协调的唯一流程结束后，本次只读复核以下已有输出，没有重跑流程：

- `freeze_06.exit`为0，原始日志写最终06及649份源码。
- `build_review_06.exit`为0；日志中链接前后两次绑定的记录SHA均为上述`fbf71fae…c1fa0`，并写`NEW_06_PROBE_LINKED=1`、`RK_RUNTIME_VERIFIED=0`。
- `checker_dependency_check.exit`为0，日志限定为电脑侧依赖闭包检查，板端运行仍待验。
- 实际专项程序文件SHA为`c920b7ca80ba0bd5f810ef6f57f4ff7e94f34abac32c14fe1820c449fe7c8021`，与`review_binary.sha256`和`execution_06_result.json`一致；已有ELF检查输出为64位小端AArch64可执行程序。
- 实际专项cpp仍为`621aedd2521a8e7460920ea657025a54652c890e14148375c9203eac731fac65`，AD观察头仍为`c1982e4435f5332f7103c5c5fad27dc1df9b61a6c02a872639ff0288433595a0`，均与prepared和最终执行结果记录一致。
- 实际`link_provenance.json`文件SHA为`c7c5e9fec96e59d68ed8c9d978f5858129fe0e63db6d5f630959f00769d63b0f`，与执行结果记录一致。
- `execution_06_inputs_before.json`和`execution_06_inputs_after.json`直接计算出的文件SHA完全相同，均为`fb5b4a81e13de098f6ee0aeb79448405f2057b8fb7f32cfdf4d3753fefdec4f2`。记录内前后均为649项源码、267个对象，另含56份依赖及7份归档运行组件指纹，没有出现只写“未变”却缺前后清单的情况。
- `execution_06_result.json`仍写`product_executed=false`、`board_accessed=false`，不把专项构建退出0扩大为联机验证。

后续由总协调安排获准的板B联机验证。本文不说明实际账户认证、真实检测状态、仪器机械功能或AD读数的物理有效性。
