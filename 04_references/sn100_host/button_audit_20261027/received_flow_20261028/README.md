# 新收回的RK业务证据复核

本目录只核整合聊天已执行的结果，未启动新产品、旧按钮扫描或额外GUI。完整判断见[review.json](review.json)，可读说明见[按钮核查文档](../../../../01_docs/knowledge/sn100_buttons_rk_review.md#received-flow-review-20261028)。

- 设置页`settings_03`：候选主程序f551、驱动5b9e，11/0、退出0；三页的独立业务和真正main的启动重开分开。
- 任务页`task_chain_02`：同候选主程序、驱动3a65，5/0、退出0；正式frmMain由驱动构造，连接本进程匿名TCP端。真正main另做启动重开，没有在该子进程内执行这些按钮。
- 后台串联6/0和原配置49/0分别保留独立接口范围，不合并成按钮通过率。

`settings_test_source.cpp`、`task_test_source.cpp`是核过指纹的测试源证据副本，防止后续计时/截图补丁覆盖旧测试依据；正式维护仍在`06_programs/sn100_host/tests/`或`samples/tests/`，本目录不构建。`SHA256SUMS`核复核JSON与两份固定测试源。原始日志和失败直接引用整合的长期归档，没有复制运行库、患者或认证内容，也没有覆盖01至08及旧367项证据。
