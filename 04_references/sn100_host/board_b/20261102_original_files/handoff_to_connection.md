# 原资料工作交接：由程序更新聊天完成最后的读取与连接验收

本聊天的文件迁入、参数修改记录、RK只读核对和日常启动入口修改已完成。用户已明确授权两个聊天协调，程序更新聊天继续负责SN500编译、正式包部署、实屏启动与连接；不要把此交接当作连接已经通过。

## 已固定的输入

- 板B序列号8314b1e8ffd0321f，SSH指纹SHA256:S4ihaw0V/Z8w7v3VlJOQEFLEH3Hp6nr3KjiUTxanwPg。
- 原资料根`/home/rkshare/programs/sn100_shell/runtime/data`，141份文件、29份原库；`.sn100-original-work-v1`内容和original_project_root已设置。
- 原测试资料保留在同板`runtime/previous_test_data_20261102`，当前不再用作原资料根；参考原件只读不改。
- theme_config.ini逐字节等于用户最新附件，SHA256为029605f35924de646b5402f1e377c93cb26e01952e0daa5fda300fe47f7feed7。SN500、42位、普通5孔/急诊5孔、24通道、报告方式1。
- config.ini的tcpmain/ip和tcplog/ip均127.0.0.1，主端口5000。其他键及另外139份文件与原141清单一致，七键变化已留证。
- sn100.sh已安装正式rk_control.sh的新入口：有original_project_root时不追加offlineAdmin，保留原登录和账户。不要用旧脚本覆盖。原config_Debug.ini的login/open=true、defaultPermission=0，未改登录权限。

## 已在板B完成的检查

复用正式original_files/check.cpp和original_ini.cpp交叉编译检查器，在RK普通用户lckfb执行。141份文件指纹/格式、29库只读quick_check、四份INI共134项值与类型核对全部通过；失败0、兼容告警0、实际退出0。检查后141文件仍字节一致；Windows root共享实际读到四INI，指纹与工作清单全部一致。

原通道配置库test_channel有25行，其中24行启用，原表保持；没有启用原远程访问、认证、LIS或打印服务。本聊天未发送主控或机械动作命令，未启动/停止主控。旧上位机当前已关闭，应用发布指向仍ef294a92，等你更新。

## 由程序更新聊天完成并登记

1. 新包编译和正式安装通过后，以当前日常入口使用原资料启动；核进程参数有--original-db-root，原根正确，不含--test-session/--seed-anonymous；连接和主题指针指向原根的config.ini和theme_config.ini。
2. 核主程序日志的原INI读取、原库初始化、SN500主题及正常就绪；保存实际主程序/发布指纹、启动和退出结果，按同一原资料重开。
3. 核127.0.0.1:5000的实际连接与状态/版本TX-RX、SN500/24通道/5+5孔，实屏42位显示及按钮限制。开始/暂停完整业务、真实仪器和长稳分开登记。
4. 最后按verification/board_b_original_files_20261102/FILES.sha256或有效清单复核配置；若程序正常写入数据库，说明实际变化，不用旧整库指纹假称不变；不得输出患者、账户和认证字段。

本次固定证据：`04_references/sn100_host/board_b/20261102_original_files/`。有限恢复输入：`07_backups/sn100_host/board_b/original_files_20261102/`。同一移植记录锚点board-b-original-files-20261102；计划当前板B卡保留程序读取/连接未验复选框，由你得到实际结果后勾选或更新，不重复创建计划。
