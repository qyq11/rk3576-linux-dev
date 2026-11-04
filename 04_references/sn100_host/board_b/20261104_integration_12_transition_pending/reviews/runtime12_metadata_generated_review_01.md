# 第12批metadata实际生成脚本独立复核

结论：本次实际生成的metadata脚本与批准核心、单阶段授权、已审模板和嵌入原件精确一致，未见阻点，可交Root独立执行该单阶段。它只安装元数据并在前后核对C7及同板资源，不调用停旧、切current或启动新版入口；不替后续任何阶段预填回执。

本次只读产物、输入和源码，在内存中解码/比对数据并做bash -n静态语法检查，只新增本报告。未改产物/输入，未导入或运行候选、生成器、阶段脚本、产品、SQL或SSH，没有联板。Root随后真实执行的结果需另记，不能用本报告或Host生成0代替板端退出。

## 实际固定字节

生成目录 `temp/sn100_host/workflow_next_20261103/runtime_transition_root_review_12_01/generated/metadata_01/`：

| 原件 | 实际SHA-256 / 字节数 |
| --- | --- |
| `control.sh` | `f1645a12099d3dfbb68ed1cfb62cda309d5b6d027368eca7500e16d5e32504ab` / 63394 |
| `generation_manifest.json` | `57345928d0a444ef8edf48edb64b0109a0868619d931a47c9db9540bd1f996da` / 12590 |
| Root核心frozen_01 | `3446ab38e1448c076b825efcdc2e6e2990600967d531fe00d24964a661d2bab7` / 9604 |
| metadata单阶段授权 | `1c255a5ac6021138ee9d339b51a42b3ea675492a8fd57d14617f63f72d5ce488` / 2029 |
| Root供审10代码快照 | `f6163b5a3fb07d4530ee49430ec92e4763b974a61c7da14f7de1800d39b40e27` / 3944 |

control读取起止SHA不变。清单49项read_inputs以及头部core/auth/generator/verifier/common/body/script记录的实际SHA和字节数全部吻合，Root快照10代码也10/10未漂。生成目录中的core_inputs.json、phase_authorization.json分别逐字等实际冻结核心和授权原件。

Root核心与已审e66b草案的JSON值仅frozen从false变true，其他目标/9:47旧C7与资源/配置/来源值没有变化。授权确为phase=metadata、frozen=true、allowed=true，准确绑定3446核心和Root批准cfca3910…；所有前阶段和旧停/新启动相关空record继续为空，没有伪造future停止文件或完成回执。

Host生成原 `.exit` 实读0、stderr为空；原stdout指向本f164脚本并注明script_generated=true、board_executed=false，对应Root提供的Tool7510c9原调用。这是Host生成事实，清单中的board_executed=false也是生成时点事实，不冒称板端metadata已成功。

## globals、数组和全部嵌入内容

直接解析实际control的数据区，52个标量globals的键和值完整等实际core/auth所需值，无额外或丢失变量；3组数组也逐项、顺序一致：七配置路径、三pointer SHA/路径、camera与3A的两行完整身份。包括phase=metadata、准确旧run/PID/ticks、旧C7/current、主控/相机/服务、6922及fb1/74b/e358新目标、nested/flat路径、观察目录/ID和90秒等待器值；没有用当前新身份重贴旧基线。

六项base64逐一严格解码，实际解码字节及SHA全部等下列原件，未执行解码出的任何脚本：

- core_base64 → 实际3446核心。
- authorization_base64 → 实际1c255 metadata授权。
- approval_base64 → 授权所绑的Root批准原文cfca3910…。
- stable_intent_base64 → 实际8268稳定24字段意图。
- configuration_base64 → ad11七配置指纹清单原文。
- waiter_base64 → eff1fdd观察等待器原字节。

稳定目标意图仍保持四动作许可false和不限时留屏目标。globals中的phase_allowed=true只来自这一次metadata单阶段批准，不把稳定意图改成停旧/切版/启动许可；previous_phase、前回执/批准、旧停收据、新run/PID/ticks对应globals均空。

## common、legacy七函数和metadata主体边界

用已审common原字节、上述实际globals、原prepare02的精确函数边界和metadata body，在内存中重新组合文字，完整结果逐字等63394字节control，无额外顶层命令、未替换标记或CR行尾。

legacy段确实只取prepare02中唯一 `checked_record() {` 起至其后首个顶层 `checked_record "$root_transition_approval"` 调用之前；七函数为checked_record、transition_process_exe、transition_controller_pid、transition_start_ticks、transition_uid、check_controller、transition_assert_quiet。原prepare02 SHA仍a6af67…，没有把其旧顶层prepare/activate等动作带入control。common和phase_metadata body分别逐字等Root快照dd304e…、abb6d4…，不是另抄旧10批完整运行脚本。

本阶段实际可达主体为：require_phase metadata → 仅immutable_store锁 → check_live_old、旧包及nested新包完整性核对 → 不可覆盖地创建本轮metadata目录 → 安装核心、稳定意图、七配置清单、等待器和授权/批准原文 → 复核同一C7、原主控、camera/3A、配置/指针及包 → 生成metadata收据。

old/new release检查先核版本清单及helper/main/renderer摘要；source已核helper后只调verify_release，helper本身受BASH_SOURCE主入口保护，不调用start/supervise。metadata主体不取生命周期锁，不关闭旧程序，不复制flat版本、不写current、不启动/停止主控或相机。创建的只是metadata/观察采集目录及冻结文件，已有目录/收据/阶段现场均拒绝覆盖。

收据路径在metadata专属namespace；本phase结束前case仍走check_live_old，细节固定为METADATA_INSTALLED=1、CURRENT_CHANGED=0、PROGRAM_STARTED=0、CLOSE_REQUESTED=0，必须在真实检查和写入成功后才产出。脚本没有预制这些已执行文件；Root实际运行后仍需回收原退出、日志和收据，才可生成下一phase。

实际执行 `bash -n control.sh` 退出0，仅证明当前文本语法。未把它记作板端运行；本审也不替Root现场再次核SSH指纹、序列号、boot和准确进程/配置。后续prepare、stop、activate、launch、observe各自仍须按原授权和真实前一阶段0回执接续，不能因本metadata审查通过而整串自动放行。
