# 急诊初建容器：可选只读观察候选

`status_login_review_08_emergency_container_candidate.cpp`从固定第07轮检查源码生成新私有副本，只增加一个观察函数、早期调用点和结果字段。原第07轮源码、正式程序、默认容器七文件候选及Task候选都未修改。当前没有最终08绑定、编译、运行或网络结果。

现有接口已经足够：主窗口的`samplePage()`返回样本页；该页有`originalProfile/isReady/isBusy/hasUnsavedChanges/snapshot`及`originalRoot`属性。观察在GUI线程、启动就绪且实际Qj为空闲时进行，位置在检查器首次取消登录卡及其他页面动作之前。它只复制页面当前快照，不调用reload、Store初始化、建架、保存、容器设置或任务方法。

main里已验的`RuntimeSettings`是局部量，没有直接只读getter。本候选不声称读到了同一内存bool，而是用既有配置加载器对同一原资料根只读重验。`config.ini`与`theme_config.ini`的SHA须等于本次`ini_before.sha256`中的启动前记录，且观察前后相同；仅提取`user.containerIsTube`和急诊孔数，不保存其余RuntimeSettings或INI内容。加载器原有的INI兼容解析可能使用私有临时副本，本候选不修改原INI、不触发数据库或设备操作，也不输出解析错误原文。

只选择`emergency=true/id=1/name=Emergency`的唯一架；每孔须是无样本身份、无任务、普通类型的干净状态，再按当前bool的true→试管0、false→杯1，核全部孔号/孔数及实际`container`。结果只记录孔号、容器数字、当前bool、期望值和必要SHA，不记录样本名、患者身份或任务内容。当前配置缺键时沿原加载器false语义，只能说明该默认分支，不能说另一值也通过。

这项观察是启动就绪点的干净急诊快照及对应初始化源码链，不能直接证明main局部bool、可见下拉框或实屏，也不验证普通新架、既有孔读回、另一bool值、保存重开、手选或CAL清空差异。若已出现样本/任务/转换、页面忙或指纹不符，候选明确记`pending`，不将其计为通过。

当确实具备条件并执行容器比较时，才向`checks`追加一个实际pass/fail；条件不足只保存pending原因。最终`check_count`来自`checks.size()`，不能预写25或仍硬称24。未来08汇总须分别报告基础项和本观察的状态：基础检查通过不代表pending的容器观察通过。最终采用哪个cpp必须由总协调在第五片实际整合和官方08输入到齐后确认，再重新绑定源码SHA、正式全对象及运行结果。

`emergency_container_observation_candidate.patch`仅展示相对固定07观察源码的增量，不是对正式产品打补丁的脚本。`emergency_container_observation_candidate.json`记录来源和候选SHA，所有正式08身份/数量仍空。当前只做源码/差异核对，没有以模拟、重放、数据库夹具或改变INI制造通过。
