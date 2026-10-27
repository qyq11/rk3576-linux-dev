# SN100全部按钮核查证据

本批于2026-10-27开始。当前只保存源码清单和原Windows对照，尚未运行本批RK鼠标检查，不把这些文件当作按钮功能通过证据。

用户已选择等待原数据库接入新版发布后再检查。共享编译/显示的释放条件见`temp/sn100_host/original_db_20261027/integration/coordination.json`。本聊天已建立每2分钟查看发布条件的接续检查，条件不满足时不抢占资源、不反复通知。

| 文件 | 用途 |
| --- | --- |
| [按钮逐项清单.csv](按钮逐项清单.csv) | 45份页面/弹窗中的367个按钮和选项，包含名称、原因和待RK状态。 |
| [button_review.json](button_review.json) | 同一清单的结构化版本，含源文件、行号和原版对照。 |
| [original_handlers.json](original_handlers.json) | 255个原版有效点击函数；已注释掉的函数不当成可执行处理。 |
| [form_button_differences.json](form_button_differences.json) | 45份对应原UI有366个控件，当前仅额外增加医院重读按钮。 |
| [original_unbound_candidates.json](original_unbound_candidates.json) | 19个需继续核批量连接/事件过滤器的候选；不能据此直接断言原版无动作。 |
| [static_input_hashes.json](static_input_hashes.json) | 盘点时当前源码/界面的文件指纹，后续部署版本变化时要重新绑定。 |

人读的汇总见[按钮核查文档](../../../01_docs/knowledge/sn100_buttons_rk_review.md)，进度仍看原移植计划和同一移植记录。后续加入RK实际发布编号、事件、退出码、匿名截图及资源采样时，另存实际运行批次，保留本次待验检查点。
