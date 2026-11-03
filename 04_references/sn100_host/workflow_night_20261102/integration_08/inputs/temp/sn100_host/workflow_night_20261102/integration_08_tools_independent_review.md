# 第八轮工具候选的独立只读复核

复核时间：2026-11-03 11:24（北京时间）。本次只读取第八轮工具候选和交接文件，重新计算文件指纹；仅新增本说明。没有导入或执行候选脚本，没有执行任何构建、上传、归档阶段，没有联网、开业务数据库或运行产品；未修改第七轮工具、正式源码或其他候选。

结论：**当前模板未发现确定的阻断问题。配置仍为`frozen=false`，不能据此开始执行第八轮流程。**最终主窗口增量、源码清单、真实晋级完成记录和板端当前版本尚未绑定；这些数据齐备后仍要核对其实际值与本模板的字段结构。

## 本次读取的确切原件

目录为`temp/sn100_host/workflow_night_20261102/integration_08_tool_candidate/`。交付清单`candidate_manifest.json`共3723字节，SHA-256为`7e71a6e899a6d253aac0f21a03bde15a27971eae88c5b85cd35c61786db3042c`。清单所列11份文件在复核前、复核后均逐项核对字节数与SHA，差异0；清单自身前后也相同。

| 文件 | SHA-256 |
| --- | --- |
| integration_08_tool_support.py | bf8ba15869c2c7977cdabf925cf62abca8129ad33000abc5ac8fbad73d159d46 |
| build_integration_08.sh | 0d9f8ab7e7404fe089245274940bfd100f43de8074c1d8434455f6af4fe1a1b7 |
| verify_integration_08_inputs.py | dc4bbaba247b62235cc8841fc47f0b7ec7dd674f047f8612eecef20ce7dd0fc0 |
| stage_board_b_release_08.py | 3c7c7d9d8ed8c5dbfaee598c5adffe6c2b03d3dc961ba77e949662ef455b0be5 |
| stage_board_b_release_08.sh | 5f59565619e57e6be8ab3e54a029095c47194e528146ab1e116ca8e4c8e79833 |
| archive_integration_08.py | 4a3a8b004e06e334e44c23cf1060288807ae593944875e2a97f81c455ffb6e2a |
| verify_and_save_integration_08.py | 9e8fc35b9b8918a34e4968accaf0e90fd51338bf087a43a640c2a1ca958c779e |
| finish_integration_08_archive.py | 7e99d546700dee9bd0849ce68479c9729d183d4655bdef5761a5ffadce24386a |
| integration_08_tools_config.json | d3b6515700be1b270c7afdb4bba19c188dc0ba2c70279b01a69aa14750f30bbc |
| handoff.md | 03190b72a478a342c1f1e1888ca623b2f5f2d55a39a4e39d16e80eb037a92643 |
| validation.json | 297090f21581cbb0443fb85bd782f144589408c22fd731cc851218a9023c7eef |

## 已沿源码核对的衔接

- **目录层级一致。**`integration_08_tool_support.py:11–18`以工具所在目录为TOOLS、父目录为TASK，再取TASK的上三级仓库根；WORK为父目录中的`integration_08`。`build_integration_08.sh:6–11`使用同一层级。恢复包、长期证据均指向新的08目录，不沿用07目录。
- **未冻结时先拒绝。**共用工具`:105–164`在配置不是本批格式或`frozen`不是真值时直接停止；构建脚本`:15–17`先做该预检，之后才建WORK。stage、archive、collect、finish的入口均先调用同一配置核对，不把空占位值当默认有效值。
- **最终源码和晋级事实须同版。**共用工具`:109–139`核根明确确认的审查SHA、底稿/最终完整文件表、每片补丁及前后检查点、连续动态序号；`:183–205`再核实际晋级`complete/phase/current_patch`、最终和底稿表、逐片应用记录、Git配置、文件属性及真实晋级脚本SHA。没有固定671份源码或固定对象数。
- **实际编译输入和对象由真实清单推导。**`verify_integration_08_inputs.py:57–114`核五类实际输入及主程序/报表入口都在完整源码快照内；`:117–133`按实际普通源、Q_OBJECT头、QRC和独立辅助源推导对象集合，并核ARM64可重定位文件。`:136–173`核双程序、静态依赖、包名和真实包SHA，收尾再次核配置、晋级及全部正式源。构建只走交叉编译、静态依赖和打包，没有产品运行入口，也没有缺环境后自动恢复或使用旧包的分支。
- **只保存到板B独立版本。**`stage_board_b_release_08.py:42–60`核专用公钥文件只有指定别名和已确认指纹，`:113–160`在联网前核本批配置、晋级、实际构建结果和包成员。`:220`将`remote/package_sha/release/expected_current`四个值原样作为四个参数传入远端脚本。板端脚本`:4–29`先核序列号、参数范围、current及部署锁；`:51–66`在保存前后再核current、包SHA和发布清单，没有切current或启停程序。上传、暂存实际退出码和原输出分别独占保存，失败不登记完整终态。
- **正式恢复归档晚于实际保存。**`archive_integration_08.py:130–216`先核已完成的真实stage和完整构建/包/源，再拒绝已有目录/完成记录；`:217–284`才创建新归档，逐文件核源码包和程序包实际内容，保存与实际构建同源的说明、清单及完成记录。不纳入运行数据。
- **证据按目的位置保存原文。**`verify_and_save_integration_08.py:78–89`先建目标父目录，再排他写入并核源/目标SHA；`:155–180`以目标位置登记复制计划，同源原件可在根级和inputs两个必要位置分别保留，不因以源路径作键而丢一份。`:224–283`只在前序完整终态不变且目标不存在后新建证据和清单，不覆盖旧文件。
- **历史失败按本批明确清单保留。**共用工具`:157–164`及collector`:32–43`核失败原件必须同时在最终审查和冻结evidence中。08可以明确给空数组；未发现06恢复流程、07固定失败槽、14ce/94d旧current或663/671/277/267固定数量残留。
- **最后收尾只核已有原件，再新建完成记录。**`finish_integration_08_archive.py:15–68`核build、stage、archive、collection终态、双来源provenance原文字节、两份完整文件清单及双压缩包Git LFS三项属性；随后再核前序与文件集合没有变化，最后排他新建finish记录，不重写已有README或SHA清单。

## 最终数据齐备后仍须核对

1. 最终审查与实际晋级格式名称、字段结构要符合交接合同；不能只填任意format文字绕过结构核对。
2. 根须给出准确的`final_review`、完整`final_source_manifest`、官方底稿包/快照、真实晋级脚本/完成记录，以及完整`releases/<64位SHA>`形式的current保护值；不能从版本前缀猜测。
3. 最终review须显式带`historical_failed_checks`，没有失败也写`[]`；collector按该必需字段读取。冻结`evidence/tools`须覆盖所有实际原件和8份工具，不能只把当前候选清单当最终输入。
4. 本说明只核模板源码和文件身份。没有执行冻结预检或任何阶段，未新增编译、包成员实际回读、网络、板端、业务或真实仪器通过结论。最终输入绑定后，仍须按build→stage→archive→collect→finish逐步核真实输出和退出码，任何失败保留现场并停止后续。

上述行号以表中精确SHA版本为准；文件发生变化时需重核受影响部分。本说明不改变第七轮24项软件专项和下一轮真实任务/N业务分别待验的边界。
