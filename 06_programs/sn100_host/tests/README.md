# SN100公共与整合测试

这里保存测试源码和固定输入。某个测试文件存在，只表示有这组验证代码；实际通过范围必须查对应日期、环境和源码版本的记录。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [account_boundary_test.cpp / account_boundary_test.pro](account_boundary_test.cpp) | 账户操作与权限边界专项测试源码。同名.pro定义该测试的构建目标。 |
| [account_test.cpp / account_test.pro](account_test.cpp) | 账户存取专项测试源码。同名.pro定义该测试的构建目标。 |
| [alarm_history_page_test.cpp / alarm_history_page_test.pro](alarm_history_page_test.cpp) | 告警历史查询页面专项测试源码。同名.pro定义该测试的构建目标。 |
| [alarm_history_store_test.cpp / alarm_history_store_test.pro](alarm_history_store_test.cpp) | 已有告警历史读取专项测试源码。同名.pro定义该测试的构建目标。 |
| [build_shell_drivers.py](build_shell_drivers.py) | 生成统一程序专项验证驱动。 |
| [calibration_reference_test.cpp / calibration_reference_test.pro](calibration_reference_test.cpp) | 校准对项目/试剂的引用保护专项测试源码。同名.pro定义该测试的构建目标。 |
| [calibration_shell_test.cpp / calibration_shell_test.pro](calibration_shell_test.cpp) | 统一程序的校准操作与处理代码连接的专项测试源码。同名.pro定义该测试的构建目标。 |
| [catalog_shell_test.cpp / catalog_shell_test.pro](catalog_shell_test.cpp) | 统一程序的项目/试剂操作与处理代码连接的专项测试源码。同名.pro定义该测试的构建目标。 |
| [client_silence_test.cpp / client_silence_test.pro](client_silence_test.cpp) | 只读客户端静默/超时情形专项测试源码。同名.pro定义该测试的构建目标。 |
| [fixtures/](fixtures) | 匿名固定输入；不是可变运行库。 |
| [history_page_guard_test.cpp / history_page_guard_test.pro](history_page_guard_test.cpp) | 报告页面的草稿及忙状态守卫专项测试源码。同名.pro定义该测试的构建目标。 |
| [history_pdf_mapping_test.cpp / history_pdf_mapping_test.pro](history_pdf_mapping_test.cpp) | PDF文本映射专项测试源码。同名.pro定义该测试的构建目标。 |
| [history_pdf_text_test.cpp / history_pdf_text_test.pro](history_pdf_text_test.cpp) | PDF文字输出专项测试源码。同名.pro定义该测试的构建目标。 |
| [history_report_test.cpp / history_report_test.pro](history_report_test.cpp) | 报告文档和输出专项测试源码。同名.pro定义该测试的构建目标。 |
| [history_test.cpp / history_test.pro](history_test.cpp) | 历史结果存取专项测试源码。同名.pro定义该测试的构建目标。 |
| [instrument_page_test.cpp / instrument_page_test.pro](instrument_page_test.cpp) | 仪器页本地操作专项测试源码。同名.pro定义该测试的构建目标。 |
| [integrated_report_test.cpp / integrated_report_test.pro](integrated_report_test.cpp) | 统一程序报告操作与处理代码连接的专项测试源码。同名.pro定义该测试的构建目标。 |
| [integrated_shell_test.cpp / integrated_shell_test.pro](integrated_shell_test.cpp) | 多功能统一操作与处理代码连接的专项测试源码。同名.pro定义该测试的构建目标。 |
| [language_setting_page_test.cpp / language_setting_page_test.pro](language_setting_page_test.cpp) | 语言设置页面专项测试源码。同名.pro定义该测试的构建目标。 |
| [lis_response_test.cpp / lis_response_test.pro](lis_response_test.cpp) | LIS本地操作响应专项测试源码。同名.pro定义该测试的构建目标。 |
| [lis_settings_shell_test.cpp / lis_settings_shell_test.pro](lis_settings_shell_test.cpp) | 统一程序LIS设置操作与处理代码连接的专项测试源码。同名.pro定义该测试的构建目标。 |
| [lis_settings_test.cpp / lis_settings_test.pro](lis_settings_test.cpp) | LIS本地设置存取专项测试源码。同名.pro定义该测试的构建目标。 |
| [login_test.cpp / login_test.pro](login_test.cpp) | 本地登录专项测试源码。同名.pro定义该测试的构建目标。 |
| [maintenance_store_test.cpp / maintenance_store_test.pro](maintenance_store_test.cpp) | 人工维护记录存取专项测试源码。同名.pro定义该测试的构建目标。 |
| [material_idle_test.cpp / material_idle_test.pro](material_idle_test.cpp) | 原样式控件的空闲重绘专项测试源码。同名.pro定义该测试的构建目标。 |
| [observe_linux_process.py](observe_linux_process.py) | 电脑侧采集Linux进程资源，区分真实采样与界面时间。 |
| [offline_test.cpp / offline_test.pro](offline_test.cpp) | 医院基础离线业务专项测试源码。同名.pro定义该测试的构建目标。 |
| [password_login_shell_test.cpp / password_login_shell_test.pro](password_login_shell_test.cpp) | 统一程序密码登录专项测试源码。同名.pro定义该测试的构建目标。 |
| [prepare_shell_latency.py](prepare_shell_latency.py) | 准备统一窗口响应实验的输入/驱动。 |
| [printer_response_test.cpp / printer_response_test.pro](printer_response_test.cpp) | 打印设置本地操作响应专项测试源码。同名.pro定义该测试的构建目标。 |
| [printer_settings_shell_test.cpp / printer_settings_shell_test.pro](printer_settings_shell_test.cpp) | 统一程序打印设置操作与处理代码连接的专项测试源码。同名.pro定义该测试的构建目标。 |
| [printer_settings_test.cpp / printer_settings_test.pro](printer_settings_test.cpp) | 打印设置存取专项测试源码。同名.pro定义该测试的构建目标。 |
| [project_reference_atomic_test.cpp / project_reference_atomic_test.pro](project_reference_atomic_test.cpp) | 项目变更与并发引用保护专项测试源码。同名.pro定义该测试的构建目标。 |
| [protocol_soak_timing_test.py](protocol_soak_timing_test.py) | 连续通信诊断脚本的时序核对。 |
| [protocol_test.cpp / protocol_test.pro](protocol_test.cpp) | 协议编码/解码专项测试源码。同名.pro定义该测试的构建目标。 |
| [protocol_timing_test.cpp / protocol_timing_test.pro](protocol_timing_test.cpp) | 协议时序专项测试源码。同名.pro定义该测试的构建目标。 |
| [qc_history_core_test.cpp / qc_history_core_test.pro](qc_history_core_test.cpp) | 质控历史解析/选择/统计核心专项测试源码。同名.pro定义该测试的构建目标。 |
| [qc_history_gui_固定测试数据.h](qc_history_gui_fixture.h) | 质控历史界面测试使用的固定构造辅助。 |
| [qc_history_page_test.cpp / qc_history_page_test.pro](qc_history_page_test.cpp) | 质控历史页面专项测试源码。同名.pro定义该测试的构建目标。 |
| [qc_history_store_test.cpp / qc_history_store_test.pro](qc_history_store_test.cpp) | 质控历史读取专项测试源码。同名.pro定义该测试的构建目标。 |
| [qc_local_page_test.cpp / qc_local_page_test.pro](qc_local_page_test.cpp) | 质控本地页面专项测试源码。同名.pro定义该测试的构建目标。 |
| [qc_local_response_test.cpp / qc_local_response_test.pro](qc_local_response_test.cpp) | 质控本地响应专项测试源码。同名.pro定义该测试的构建目标。 |
| [qc_local_shell_test.cpp / qc_local_shell_test.pro](qc_local_shell_test.cpp) | 统一程序质控操作与处理代码连接的专项测试源码。同名.pro定义该测试的构建目标。 |
| [qc_local_store_test.cpp / qc_local_store_test.pro](qc_local_store_test.cpp) | 质控本地存取专项测试源码。同名.pro定义该测试的构建目标。 |
| [qc_reference_test.cpp / qc_reference_test.pro](qc_reference_test.cpp) | 质控对项目/试剂引用专项测试源码。同名.pro定义该测试的构建目标。 |
| [read_only_client_test.cpp / read_only_client_test.pro](read_only_client_test.cpp) | 只读客户端专项测试源码。同名.pro定义该测试的构建目标。 |
| [read_only_messages_test.cpp / read_only_messages_test.pro](read_only_messages_test.cpp) | 只读报文字段专项测试源码。同名.pro定义该测试的构建目标。 |
| [report_dictionary_dialog_test.cpp / report_dictionary_dialog_test.pro](report_dictionary_dialog_test.cpp) | 报告字典选择窗专项测试源码。同名.pro定义该测试的构建目标。 |
| [report_dictionary_page_test.cpp / report_dictionary_page_test.pro](report_dictionary_page_test.cpp) | 报告页字典选择操作与处理代码的连接专项测试源码。同名.pro定义该测试的构建目标。 |
| [report_dictionary_store_contract_test.cpp / report_dictionary_store_contract_test.pro](report_dictionary_store_contract_test.cpp) | 已提交医院字典查询接口专项测试源码。同名.pro定义该测试的构建目标。 |
| [report_remaining_html_test.cpp / report_remaining_html_test.pro](report_remaining_html_test.cpp) | 报告HTML输出专项测试源码。同名.pro定义该测试的构建目标。 |
| [report_remaining_html_verify.py](report_remaining_html_verify.py) | 核对实际HTML输出。 |
| [report_remaining_shell_test.cpp / report_remaining_shell_test.pro](report_remaining_shell_test.cpp) | 统一程序报告剩余功能操作与处理代码连接的专项测试源码。同名.pro定义该测试的构建目标。 |
| [report_remaining_test.cpp / report_remaining_test.pro](report_remaining_test.cpp) | 报告显示/隐藏等本地功能专项测试源码。同名.pro定义该测试的构建目标。 |
| [report_remaining_verify_csv.py](report_remaining_verify_csv.py) | 核对实际CSV输出。 |
| [result_ingest_store_test.cpp / result_ingest_store_test.pro](result_ingest_store_test.cpp) | 原值结果预约与存储专项测试源码。同名.pro定义该测试的构建目标。 |
| [rk_deployment_test.sh](rk_deployment_test.sh) | 部署从启动、运行到关闭的过程专项验证；具体临时/模拟路径先读脚本，不能当日常启动命令。 |
| [run_protocol_soak.py](run_protocol_soak.py) | 电脑侧组织连续通信实验和日志收集。 |
| [sample_combination_mode_test.cpp / sample_combination_mode_test.pro](sample_combination_mode_test.cpp) | 普通/多瓶组合预设模式专项测试源码。同名.pro定义该测试的构建目标。 |
| [shell_account_test.cpp / shell_account_test.pro](shell_account_test.cpp) | 统一窗口账户操作与处理代码连接的专项测试源码。同名.pro定义该测试的构建目标。 |
| [shell_latency.sh](shell_latency.sh) | 统一窗口响应测量入口。 |
| [shell_latency_test.cpp / shell_latency_test.pro](shell_latency_test.cpp) | 统一窗口操作响应专项测试源码。同名.pro定义该测试的构建目标。 |
| [shell_test.cpp / shell_test.pro](shell_test.cpp) | 主窗口基础行为专项测试源码。同名.pro定义该测试的构建目标。 |
| [shutdown_request_test.cpp / shutdown_request_test.pro](shutdown_request_test.cpp) | 正常关闭请求守卫专项测试源码。同名.pro定义该测试的构建目标。 |
| [status_loopback_shell_test.cpp / status_loopback_shell_test.pro](status_loopback_shell_test.cpp) | 统一窗口本机模拟状态专项测试源码。同名.pro定义该测试的构建目标。 |
| [status_loopback_test.cpp / status_loopback_test.pro](status_loopback_test.cpp) | 本机模拟状态收发专项测试源码。同名.pro定义该测试的构建目标。 |
| [status_replay_shell_test.cpp / status_replay_shell_test.pro](status_replay_shell_test.cpp) | 统一窗口文件回放操作与处理代码连接的专项测试源码。同名.pro定义该测试的构建目标。 |
| [status_replay_test.cpp / status_replay_test.pro](status_replay_test.cpp) | 离线状态回放专项测试源码。同名.pro定义该测试的构建目标。 |
| [tcp_fault_test.cpp / tcp_fault_test.pro](tcp_fault_test.cpp) | TCP异常恢复专项测试源码。同名.pro定义该测试的构建目标。 |
| [tcp_transport_test.cpp / tcp_transport_test.pro](tcp_transport_test.cpp) | TCP本机传输专项测试源码。同名.pro定义该测试的构建目标。 |
| [test_arm64_check.py](test_arm64_check.py) | ARM64依赖检查工具的专项验证。 |
| [test_arm64_object_reuse.py](test_arm64_object_reuse.py) | ARM64对象复用工具的专项验证。 |
| [test_shell_arm64_inputs.py](test_shell_arm64_inputs.py) | 统一程序ARM64输入清单的专项验证。 |
| [verify_history_pdf_text.py](verify_history_pdf_text.py) | 检查生成PDF中的文字。 |

## 运行与结果

从[SN100工程README](../README.md)找到对应WSL/构建入口；测试对象、生成程序、日志和可变数据库留在根temp。测试应使用明确隔离的匿名目录，不导入参考生产库。电脑离线、Windows编译、ARM64编译、RK运行和真实仪器分别登记。
