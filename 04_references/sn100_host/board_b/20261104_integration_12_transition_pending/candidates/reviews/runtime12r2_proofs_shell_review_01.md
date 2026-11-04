# 新02运行凭据采集器：远端Shell第二独审

2026-11-04。完整只读291行proof及固定helper相关定义，不只看diff；未import、生成远端脚本、执行SSH/产品/解析器或改代码。结论：未见远端业务写入/启停路径；**其作为“补充只读取证”可用，但下述两项未闭合，不能用该工具单独宣称旧C7完整停止封口及新run无退出封口已核实。**

## 两项确定边界缺口

1. **新run缺少exit_code/result.txt不存在的明确检查。**proof第143–148行截取固定helper的check_live；该函数只核新PID/start、UID、exe/SHA、current及release_id，不读新run封口。第135–139行只归档新run四身份文件、app.log及latest_run，没有拒绝exit_code或result.txt已存在（包括坏链接）。随后第184–187行固定写APPLICATION_EXIT=null和SAME_APPLICATION_RUNNING=1。进程当时确实活着的身份检查仍有效，但不能据此声称“同时实核run未封口”。最小补法是在前后check_context中按已核NEW_RUN明确拒绝这两个路径的`-e || -L`，并绑定latest_run/四身份原件；不应只靠手写null摘要。若Root另有实际postlive检查，则应将它单列为必要证据，不能把这项归到本工具。
2. **没有独立核旧C7的stop9。**file_scope第121–127行仅收远端stop的stdout/stderr/actual_exit；未收旧run的result.txt、exit_code、app.pid、app.start_ticks、app.exe、boot_id，也没有消费本轮授权的old_stop_files逐项SHA及精确0/clean1正文。read_phases只核stop回执上的四个成功字段与连续授权链，没有以这些原件复核。独立stop collector若已核实，可在最终归档中明确承担此项；若要求proof本身复核，则补固定旧C7六件和三件stop日志与本轮真实stop授权记录的逐字比对，并在前后重核。不能把旧b31或旧3446链stop79填进来。

以上不要求为取证重停C7或关闭新程序；修订只增读取/比较，或将独立真实检查明确作为最终必需证据。旧工具原件应保留。

## 已核到的只读保护与范围

- load_guard先按19245字节与21805完整SHA读取helper，再用ModuleType的非`__main__`名字执行定义。该固定文件顶层只有标准库import、路径/常量、函数/类定义与main守卫；没有顶层subprocess/网络/解析器调用，守卫为假。只复用定义，不执行helper主流程。本审只AST解析文字，没有实际动态加载。
- 真正绑定是新core652ee775、runtime_02、6922发布/fb1主程序、新观察ID；helper错误消息中遗留“3446”字样只是拒绝提示，非实际SHA/路径。没有因此混用旧3446核心。七阶段必须已有LF0、同core/授权/boot/串号/current及完整回执尾；前后阶段授权记录逐项相接。launch/observe额外复用固定helper的controller/camera/config身份核对，要求同新run/PID/ticks、supervise0和观察成功。工具不能在阶段未齐时造成功入口。
- SSH先本地核固定known_hosts指纹，再StrictHostKeyChecking/ED25519/专用别名，远端先后核串号8314b1e8ffd0321f、boot、准确新PID/start/UID/exeSHA及current规范目标和release_id。没有stop/start/kill/TERM/systemd-run/openvt、服务重启、业务命令或life/store锁；systemctl仅show，ss仅读监听，tar向stdout回收文件。
- 前后check_context还核原主控5008/118180、相机2510511/6498209及3A139148/847289的两次stat身份、UID/exe/SHA，相机服务active/running/MainPID；监听核5008的0.0.0.0:5000。七配置清单自身SHA固定ad11且sha256sum逐项校验，三指针各核固定SHA。没有读取INI正文、DB或业务目录。此处检查所指定PID，未另做所有同basename实例/所有5000持有者的全枚举，不能扩大为新的全板进程排重审计。
- 明确白名单共52份：元数据3份；七阶段各authorization/approval/receipt共21份；stop日志3份；launch日志/环境/服务7份；observe日志/环境/服务5份；activation audit7份；新run四身份+app.log，以及latest_run。只收stop、launch、observe的上述phase_logs，不枚举其他phase_logs、captures、安装树或业务资料；六LIS封口仍由独立LIS collector负责。本次没有运行file_scope，52为按源码分支静态计数。
- 白名单成员远端要求规范非链接/单链接普通文件及逐份大小，tar后再次check_context；Host压缩和解压内容32MiB、成员名/唯一性/类型/无PAX与稀疏/逐项上限检查，没有extractall。3份元数据、21份阶段记录、audit六定值、新run四身份/latest_run等已指定expected原文逐字比较，其余日志只是保存，不等于自动核完其内容。运行中的app.log可能变化导致tar非0，此时保留真实失败，不重试业务或改为成功。
- Host新输出只在HERE下全新单层目录，O_EXCL创建，源清单起止重核；先保命令、输入记录、SSH原压缩stdout/stderr和真实退出。SSH非0直接返回，不解包成成功；成功后snapshot需唯一开始/结束标记及完整尾。application_exit=None只在真实只读流程成功后输出，不预填程序退出0；它仍须受上述新run封口缺口限制。

## 固定实物

| 文件 | 字节 | 完整SHA-256 |
| --- | ---: | --- |
| runtime_batch12_02/collect_runtime_proofs_batch12_01.py | 19348 | `d08fea2553d74b51aca948d7310daa7a50712b49e6473d75a9a9008fd016453d` |
| runtime_batch12_02/collect_runtime_lis_batch12_01.py | 19245 | `21805f6c036da483cba5a0787daf67a1001fff568a15321967a130e1af6a744c` |
| runtime_batch12_02/core_inputs.frozen_01.json | 9604 | `652ee775372fea069aef634d4a80ca703c199e9c310dab839ad196c19f8f6424` |

三者实际读入符合固定值，两个Python的AST解析均0，写后再核三者稳定。本报告没有执行本工具，没有证明七阶段/取证完成、长期稳定、真实LIS、仪器、实体操作或资源门槛通过；新链实际状态由Root另收证据。
