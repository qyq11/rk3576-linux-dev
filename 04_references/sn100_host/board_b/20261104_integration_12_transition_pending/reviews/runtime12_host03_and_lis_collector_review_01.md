# 第12批Host03分流与LIS采集候选独审

结论：03对本次真实preflight日志的无损分流成立；LIS采集候选在本次静态范围内未见确定代码阻点。**当前运行链仍不能沿旧3446继续。** Root实际stop退出79，先于任何关闭请求被旧相机身份守卫拒绝；相机已自动退出重启，旧核心不再描述同一现场。这与代码静态审查通过是两回事，不能登记新run已启动或采集已执行。

本次只读代码、已有原件和元数据，做AST解析及字节/字段比对，只新增本报告。未导入或执行Host03、collector、解析器、产品、测试、SQL或SSH，没有运行/修改候选、生成脚本、原日志、核心或授权。

## Host03：真实原件及收据边界

`runtime_batch12_01/phase_host12_03.py` 起止SHA-256为 `87c8f0b7a54ad3485471c5902fe30e6996ed20f5508b9a83dd311caf8361f646`，11485字节。完整对02差异只在capture：唯一收据SHA标记后，取EXIT_CODE=0换行边界，先核收据真实SHA，再无损分开stdout前缀与尾随stderr；新增stderr导出和两导出来源record/说明。02的派发script/started/实际generated control绑定、旧停九项路径与collector来源检查均保留。

Root已说明board08原保存方式是stdout后接stderr，不是按到达时间混流。本次按该明确来源核原 `runtime12_preflight_01.log`：28176字节，起止SHA为 `a3ab90f708a0057696125632c148f432e7f2b16e9ef613ed03933dafec37f82d`，唯一RUNTIME_RECEIPT_SHA256标记，收到的1040字节收据SHA恰为 `5550935132fd1261eb3eabf0ba7766fcdcdfd0e2b72c8023ae1046f09f0d73b0`。

此原件的边界是完整单行EXIT_CODE=0，收据开始后只有这一处。stdout前缀为28024字节，stderr尾为152字节，恰好两条 `find: Failed to restore initial working directory: /root: Permission denied` 原警告；二者拼接逐字还原原合并log，没有删除警告或改原stdout/receipt。已点核本批固定phase模板及helper的实际回执字段，未出现会早于真实末行匹配的其它EXIT_CODE=0字段；SHA核对仍会拒绝不符边界，不靠截尾后强报成功。

原preflight外层exit直接读为0，started、规范化派发脚本和generated/control绑定吻合，收据core/auth摘要正确，PREFLIGHT_OK=1仅一次；回执明载OLD_APPLICATION_STILL_RUNNING=1、CURRENT_CHANGED=0、PROGRAM_STARTED=0、CLOSE_REQUESTED=0。03的CAP phase.log将只保存完整stdout，phase.stderr.log保留完整尾stderr，provenance指回原合并log/exit/started及两个导出。原退出CRLF→LF仍仅作用于已核真实0，不会把79转换成0。

这一结论限于已说明的board08连接方式和当前真实日志，不声称任意混流日志都能恢复原两路输出。03没有执行preflight，也没有通过分流重新签发一个设备执行结果。

## LIS采集候选：固定对象与进入条件

`runtime_batch12_01/collect_runtime_lis_batch12_01.py` 起止SHA-256为 `57f18cf5ac37f23dfd4b033992c4f4e6d48256d6d819696d8a0c3a1fa72d52ac`，19245字节；已读328行全部代码及相邻说明。Python只做AST解析，未执行main、remote_script或任何子进程。

工具固定core3446/9604字节、新6922发布、fb1主程序、观察ID、板B序列和SSH指纹，以及原解析器a0ea76…。在新建采集输出或SSH之前，必须读取实际launch和observe的规范0退出、完整日志/回执、相应冻结授权和Root批准，并核core/boot/版本/配置/主控/相机身份；observe必须承接同一launch的授权、回执、log和exit。两份回执必须具有相同新run/PID/start与真实旧停回执摘要，并具备原launch/observe成功字段，不能从手写默认值取得入口。

读取本地原件受普通单链接/无链接路径、字节上限、重复JSON字段及非法数值拒绝、起止不变检查约束。新输出只能在本目录下全新单层名字，不覆盖已有结果。known_hosts由本地ssh-keygen核唯一固定指纹，SSH又强制指定known_hosts、别名、ED25519和StrictHostKeyChecking；工具只会发一次只读SSH，没有产品启动、停止、切current、业务包或INI/资料写入调用。

远端脚本文字在tar前后分别核串号、boot、准确新PID/start/UID/exe/SHA、current和release_id，进程stat还在同一次check内前后重核以拒绝替换/死亡。它只对固定capture_leaf读取六个约定文件：manifest、before/after身份、status、observation JSON和ready。没有任意远端路径参数；普通/非链接/单链接及各自大小检查先于tar，不接管主控、相机或服务。

stdout压缩包、stderr与实际退出分开独占落盘，SSH非0直接保留并返回该值，不解析成成功。压缩文件和解压数据各2MiB的读取门槛、六个固定成员名/唯一性/普通类型/无PAX或稀疏字段，以及逐成员上限在展开前核；未使用extractall或tar成员路径任意写Host。网络stdout先流式落盘，2MiB是进入读取/解压的门槛，不冒称对网络写盘峰值或RSS已经实测。

取到六份原件后仍核本地来源未漂，再运行固定SHA `a0ea76de798d3fee19e5bb0adf547d033d3619f5020e05a5f61ce6cbc1c46783` 的原独立解析器，实传board/boot/PID/start/release/exe/SHA/current/release_id_file/observation ID；参数名逐项等原解析器接口。它保存解析argv、stdout/stderr和真实退出，不把SSH0当解析器0；原manifest、ready、前后身份与业务封口一致性仍由原解析器核。这些是源码中的进入门，不是本次已运行结果。

## 当前实际阻点：旧核心所绑相机已经换进程

本地原 `runtime12_stop_01.exit` 实读为79，SHA `60dae80579a054bc5a7703da02947ee145e19400d113054f3af8e654b1b1713f`；原log只有“RUNTIME_TRANSITION_REFUSED=所需运行进程缺失或重复。”，SHA `a96eecaa727b0a5b539132fe99134ad7bc9b0fb640830e8d596718779b8992e6`。Root通报Toola12748外层显示1，但原.exit的79才是本次阶段实际退出，不能填0或当作旧应用退出。

以下相机及现场补充依据Root本次只读Tool76004a/62e860协调事实，本审没有另行联板：旧camera 2046481在10:30:52连续10秒无帧后自动exit1，服务按on-failure三秒后于10:30:56成为新PID2510511/start6498209，映像SHA仍7a1283…。相机作者确认没有动作。新进程即使映像相同，也不再是3446核心冻结的2046481/start5701959。

Root同时确认C7 669382/start2102645仍活，准确oldrun没有result/exit封口，远端没有stop授权/日志目录；current仍C7，原主控、3A、ad11配置和三个资料指针保持。stop在check_live_old、任何关闭调用之前拒绝，不能说已正常停C7，更不能生成假的APP_EXIT/CLEAN_SHUTDOWN、stop receipt或继续activate/launch。

因此03当前只能诚实归档已有真实阶段，不能把这次79纳成成功；collector当前仍是未执行候选，缺真实新版launch/observe链，且仍固定旧3446核心。Root后续须按新现场单独处理核心/授权/派生输入，旧01/02/03、原失败和既有证据全部保留，不能静默把新相机贴成旧身份继续。本报告没有声明6922新run已起、采集成功、真实LIS或仪器通过。
