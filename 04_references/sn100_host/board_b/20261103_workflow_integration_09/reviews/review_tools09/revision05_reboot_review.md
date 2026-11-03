# 第09批修订05重启接续独立审查

2026-11-03。只读核05工具、配置、正式源码、七份root辅助、preparation_04接口、04中断原件和文件属性规则；仅写本报告。没有运行阶段工具、编译、产品、网络或板卡操作，没有干预后台05完整构建。

结论：05相对已审04只做独立构建目录及后续辅助路径重基，未发现需要扩大业务改动的确定阻点。04中断原件仍在，05不能用04部分对象或旧日志当作新构建完成；真实构建、对象绑定、保存与板端结果仍待各阶段实际返回。

## 重启与04原件

本次只读Windows系统查询的`LastBootUpTime`为2026-11-03 17:08:16，与root登记及用户说明一致。04没有完整内部退出、外层退出或完整产物，继续记中断和退出未知，不能改写为真实编译失败或完成。

`revision04_generated_inventory_before_cleanup.json`的374份记录已逐份读取实际文件核SHA和大小，全部仍在；对应218个`.o`、155个`.cpp/.h`生成文件和1个空`build.lock`。因此“156份非对象文件”包括锁文件，不宜全部称生成源码。root记录的自动审批拒绝删除没有被本次审查绕过，也未再次尝试删除。

## 九工具、正式源和冻结输入

- 对比`integration_09_root_frozen_04/`与05副本，只有`build_integration_09.sh`和`integration_09_tool_support.py`各一处WORK从04改05；其余七份逐字节相同。05使用`integration_09_revision_05/`独立对象与输出目录，原完整构建和禁复用守卫保留。
- 当前05配置SHA为`d8f5a6b893d5e2f36e664fadd925a5a48e33498c22831156cfad3fa8b95517d1`。75项evidence、九工具和主要引用原件均按实际SHA及声明大小核对通过。
- 批准审查、起止源码表、原基线、真实promoter/progress及完整08 current保护均与04相同。新配置只在证据上追加04中断记录及374文件清单，没有假称再执行补丁。批准03审查及其11片真实继承链保持原值。
- 正式684份源码逐份SHA仍等于批准最终表；本次不涉及业务源码修改。已见05真实输入核对日志`SOURCE_FILES_COVERED=684`和去重后的`BUILD_INPUTS_COVERED_BY_SOURCE_SNAPSHOT=392`，这仅是编译前输入核对，不是完整构建通过。

## 七份root辅助与检查器候选

对`run_build`、`run_postbuild`、`prepare_activation`、`run_checker`、`prepare_live`、`collect_live`和`archive_live`七份revision05辅助作文本对照：将04目录与场景名称改05、preparation_03改preparation_04、候选清单03改04后，文本均完全相同，没有其他逻辑变化。

`run_checker09_revision05.py`现行读取`candidate_sources_04.sha256`，不是旧03清单；指向WORK05及`20261103_status_login_09_preparation_04`。preparation_04的最终驱动EXPECTED_WORK和binder允许目录也均为WORK05。当前04候选清单的28项实际SHA核对相符；作者仍须按原分工完成冻结交接，不能把本次文件对照当成检查器已编译。

后续remote attempt与本地采集使用`auto_revision05_01`；05派发/采集/独核日志和退出名同步重基。原实际退出、固定板B身份、七配置门槛、资源采样及恢复包范围的已审逻辑均未变。

## 根属性漂移与局部规则

当前根`.gitattributes`实际SHA为`c46d8032dc7311b5911dfb7e578ad70ba1258ac924b72675f8b2192c5b9d445a`，与批准审查的root_attributes完全相同；根文件Git差异为空。没有通过改批准元数据来接受新根属性。

`04_references/sn100_host/board_b/.gitattributes`仅增加本板08权限复核与09新专项证据的`-text`局部规则。只读`git check-attr text`对两类实际路径均返回`unset`，说明这些证据按原字节保存；局部文件不属于684份正式构建源码，也没有回写冻结根属性。

root已登记根属性漂移被预检拒绝、实际退出1且未开始构建；此失败不能写成05编译失败。恢复根属性后的当前配置和源码均已按上述真实字节独核。本次未重新运行preflight，也不把root报告的预检0扩展为构建0。

下一步沿现有后台完整构建等待真实内部和外层退出，只有完整0及对象/程序/源码包绑定全部通过，才进入既有串行保存与检查器流程。保留04、03、02的原始失败或中断范围，不用新05结果倒写历史。
