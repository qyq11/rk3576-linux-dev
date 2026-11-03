# 工作环境调整证据

保存工作目录、共享主目录和终端会话调整前后的核对材料。

2026-11-02板B新增[统一root登录与503无线连接记录](root_login_board_b_20261102/README.md)：两次重启核开机密码登录、时间保持和WiFi自动连接，包含失败、恢复及无线共享复测，板A结果不套用。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [windterm_board_b_20261102](windterm_board_b_20261102/README.md) | 新增板B的LCKFB-B终端入口，保存参数实际连接及root目录核对通过；界面双击待用户确认。 |
| [ssh_password_board_a_20261102](ssh_password_board_a_20261102/README.md) | 板A开放root远程密码登录，有线/无线纯密码登录及原密钥连接均实际通过。 |
| [root_password_login_board_a_20261102](root_password_login_board_a_20261102/README.md) | 板A最新方式：开机输入root账户和密码；两个终端等待登录、指定密码验证及重启核对通过。 |
| [root_session_board_a_20261102](root_session_board_a_20261102/README.md) | 板A屏幕和三针口开机进入root，以及新增WindTerm“板A-root”会话；实际重启、账户、目录和连接参数核对。 |
| [20261022_temp_archive_manifest.json](20261022_temp_archive_manifest.json) | 该事项的固定核对材料；结合下方记录阅读。 |
| [home_path_20261022](home_path_20261022) | 该事项的固定核对材料；结合下方记录阅读。 |
| [root_session_20261022](root_session_20261022) | 该事项的固定核对材料；结合下方记录阅读。 |

详细过程见[对应记录](../../02_records/20261021_workspace_setup.md)。这里不是安装输入目录，也不是新的开发计划。

2026-10-22清单保留当时的文件指纹和归档事实；2026-10-27用户要求移除的Qt独立示例不再提供源码、图片或运行入口。SN100仍需的基础依赖现放在`07_backups/qt_environment/sn100_base_arm64/`，当前恢复方式见该目录说明；历史清单中的原位置不代表文件现在仍存在。
