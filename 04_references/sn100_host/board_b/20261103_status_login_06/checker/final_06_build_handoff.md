# 最终06检查器：已编译，等待板端运行

本次由总协调明确授权，只在电脑核对最终06输入并交叉编译检查器。冻结、编译链接和新检查器依赖检查的实际退出码均为0；没有连接板卡、上传、切换current、运行产品或改正式源码。板B有线恢复、正式包上传和部署由总协调另行记录，不能混成本次执行结果。

## 本次实际绑定

| 项目 | 实际结果 |
| --- | --- |
| 最终发布编号 | `14ce9714aaa0017c7ad78ff3f551d978672e5def79b96e817605295d7e59020c` |
| 正式主程序 SHA-256 | `f3e814b4110b14ddbaef8c4516ab8a5b1c43747c45dcd4fff54ad3af0806ec6c` |
| 报告程序 SHA-256 | `0049f525076c24d4fd3c7a6415edb447431bf8f60e816708c1403d5c436942c0` |
| 官方源码归档 SHA-256 | `c1a21586e8b9a1adaaefd9ac1743f8b0f74c04a45f37001450de61b42402369b` |
| 新检查器 SHA-256 | `c920b7ca80ba0bd5f810ef6f57f4ff7e94f34abac32c14fe1820c449fe7c8021` |
| 检查器源码 SHA-256 | `621aedd2521a8e7460920ea657025a54652c890e14148375c9203eac731fac65` |
| 独立报文观察头 SHA-256 | `c1982e4435f5332f7103c5c5fad27dc1df9b61a6c02a872639ff0288433595a0` |
| 完整链接记录摘要 | `fbf71faedb7cd39117f094e04c7c092f01649f05b6fa527b34312edff67c1fa0` |

官方源码包位于项目的`07_backups/sn100_host/board_b/workflow_integration_20261102/integration_06/sn100_sources_14ce9714aaa0017c7ad78ff3f551d978672e5def79b96e817605295d7e59020c.tar.gz`。冻结工具逐份核了649个归档成员及对应最终snapshot；检查器使用其中的头文件，没有从仍可编辑的正式目录另取头文件。

实际主对象为164份普通源文件、92份Qt自动生成的信号连接源文件、11份资源源文件，共267个连续`obj_0.o`至`obj_266.o`。真实入口保留为`obj_0.o`，其SHA-256为`6121b38519c202b59cb378409ebeb1ebd81e1167828c661e8205d826769864ae`。全部对象、生成文件和对应来源的摘要见`link_provenance.json`，不是从文件数量猜测对象绑定。

## 实际执行和证据

本批唯一执行入口是：

```bash
python3 /mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/board_b/20261102_status_login/integration_06_review_preparation/execute_final_06_build.py
```

入口实际依次执行：

```bash
python3 freeze_release_06.py /mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/workflow_night_20261102/integration_06 14ce9714aaa0017c7ad78ff3f551d978672e5def79b96e817605295d7e59020c /mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/07_backups/sn100_host/board_b/workflow_integration_20261102/integration_06/sn100_sources_14ce9714aaa0017c7ad78ff3f551d978672e5def79b96e817605295d7e59020c.tar.gz
bash build_review_06.sh 14ce9714aaa0017c7ad78ff3f551d978672e5def79b96e817605295d7e59020c
```

`freeze_06.exit`、`build_review_06.exit`均为0；日志保存在同名`.log`中。冻结、链接前后使用同一正式构建锁。两次绑定输出相同的完整记录摘要；`execution_06_inputs_before.json`与`execution_06_inputs_after.json`完全相同，覆盖官方complete/归档/SHA清单、267对象、正式依赖报告所列56文件和7项归档运行文件（其中3项插件已包含在56文件内，共60个不同文件）。这是这些已列明文件的前后核对，不代表板端实际库或驱动已核对。

新检查器另以冻结版本的`check_arm64.py`检查ARM64架构、动态库及SQLite等归档依赖；实际命令记录在`checker_dependency_check_result.json`，退出码在`checker_dependency_check.exit`为0，报告在`checker_dependencies/`。结果为静态依赖闭包通过、板端运行待验证；没有运行电脑版本、模拟主控或本机回环。

检查器是ELF64小端AArch64程序。`review_build_provenance.json`保存实际编译器、Qt库摘要及编译/链接参数；`review_elf_header.txt`保存实际ELF头。

## 交给总协调的板端输入

`qa_upload_06.sha256`只列需要一起上传到新QA目录的8个文件：新检查器、`review_release_id`、`run_case_on_board.sh`、`capture_application_exit.sh`及四个最终退出合同文件。上传后先逐项核SHA，不复用05文件；本线程没有执行上传。

根协调在确认板B序列号/SSH指纹、正式current已按授权切至本版、正确原主控仍运行且没有其他上位机后，使用本轮独立目录执行：

```bash
bash run_case_on_board.sh auto 14ce9714aaa0017c7ad78ff3f551d978672e5def79b96e817605295d7e59020c f3e814b4110b14ddbaef8c4516ab8a5b1c43747c45dcd4fff54ad3af0806ec6c c920b7ca80ba0bd5f810ef6f57f4ff7e94f34abac32c14fe1820c449fe7c8021 180 auto_01
```

脚本要求板B root安排，实际应用以lckfb运行；核实际INI、原资料和唯一正确主控，只允许自动专项主动发送精确Qj。它不会切current或恢复主控。Uh被动接收仍可能按原业务写库，不能把发送范围等同于所有数据库未变。

最终关闭合同有17条必需日志，含独立的`ORIGINAL_CHANNEL_WORKER_STOPPED true`；总线程表达式仍未包含该线程，不能只核总停止。合同JSON SHA-256为`b1b7293f42054300194264441ea897fe760051cb93b36fcd3d40f2fb5ece6152`，9条失败正则分别否决停止为假、停止未确认、通道错误、三种失败结果及窗口关闭未确认。正常取消没有混入失败枚举。

取回整轮原始结果后，用本目录`summarize_run.py`显式UTF-8解析；最终成功同时要求本代Qq独立96格对照、完整TX范围、检查/应用真实wait/timeout封装码和关闭合同全部成立。编译成功不证明这些运行条件已满足。

真实管理员密码、实体键鼠、红色退出重开、实际测试绿色闪烁和测量物理有效性仍分别待验。不得把程序化控件操作或控件截图写成这些人工/实物操作已通过。
