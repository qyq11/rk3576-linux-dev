# 板B原主控退出排查与恢复

2026-11-02，北京时间。用户明确确认“我没关，继续排查并恢复原主控”。本轮已恢复同一份正确主控；旧进程的退出原因仍未确定，不能归因为用户手动关闭。这里只确认主控进程、日志和监听端口恢复，不代表上位机、实际检测、机械动作或长期稳定性通过。

板B序列号为`8314b1e8ffd0321f`，SSH主机指纹为`SHA256:S4ihaw0V/Z8w7v3VlJOQEFLEH3Hp6nr3KjiUTxanwPg`；操作前均核对。没有操作板A。

## 查到的事实

- 23:17:09恢复前，正确主控进程不存在，5000端口没有监听；正确可执行文件仍在，内容与原确认版本一致。
- 旧运行目录`/home/rkshare/programs/aca_flow_test/runs/correct_05/live_20261102_173807/`只留有启动时间和程序日志，没有最终退出码或结束时间。日志最后输出为19:53:12；此次读取时其SHA-256为`a5850f745f5c2ba4ebfa9df316e65632c7f2988f3c4dfc67bfa568f1a015b405`。
- 原启动器通过前台SSH终端运行`ACA | tee`。17:38:05建立的SSH会话在19:53:12断开，与主控日志停止同秒。旧进程记录缺少父子进程、会话和终端关系，也没有终止信号，故只能将终端连接结束列为相关线索。
- `disconnected by user`是SSH日志原文，不能据此认定用户亲自关闭。19:52:44上位机检查结束、TCP客户端断开后，主控还继续输出约28秒，也不能写成上位机断开立即杀死主控。
- 在已查内核日志窗口中未找到内存不足、内核杀进程或段错误记录；已有core文件均早于本次事件。这不能排除未保存的其他原因。
- 两个子Agent分别检查历史启动依据和检查器进程清理范围，没有发现检查器跨进程组终止正确主控的明确路径。详见[历史核对](local_history_review.md)、[进程影响核对](checker_process_review.md)。

原始采集见`before_restore.log`、`preflight.log`。后者最后一项只读列表因传入脚本行尾格式退出1，前面的身份、依赖、配置指纹和SSH记录已取得；未把这次采集写成完整通过。后续脚本改为复制Linux格式文件后运行，旧失败记录保留。

## 如何恢复、目前运行在哪里

23:18:51建立仅本次运行使用的系统服务`sn100-original-controller-board-b.service`，调用原脚本`/home/rkshare/programs/aca_flow_test/run_correct_live.sh`。原脚本没有改动，可执行文件没有替换，配置和配套资料没有覆盖。服务使进程独立于SSH终端；`Restart=no`，没有设置失败自动重启或开机自启。

实际主控在23:18:53启动，进程号`79082`；启动脚本进程号`78407`。核对结果：

| 项目 | 实际值 |
| --- | --- |
| 可执行文件 | `/userdata/shared/ACA_V1.0OffLineD-500` |
| 文件SHA-256 | `ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff` |
| 身份及工作目录 | root；`/userdata/shared` |
| 原启动环境 | `LD_LIBRARY_PATH=/userdata/shared`，无新增业务参数 |
| 进程归属 | `/system.slice/sn100-original-controller-board-b.service`，无终端依附 |
| 监听 | `0.0.0.0:5000`，所属进程79082；本轮没有连接该端口发业务报文 |
| 新运行日志 | `/home/rkshare/programs/aca_flow_test/runs/correct_05/live_20261102_231853/program.log` |
| 启动器最新日志指针 | 原脚本正常更新`correct_05/latest_live_path.txt`至上述新目录；旧运行目录及旧日志保留 |
| 运行终态 | 核对时仍运行，尚无最终退出码；服务显示中的0不能当作已正常退出 |

两次独立SSH采集分别为23:19:39和23:20:30，均找到唯一正确主控、5000监听和继续增长的日志，均退出0；启动时的SSH早已结束。23:21:27再固定日志及服务状态，主控仍运行。原日志与采集见`after_disconnect_01.log`、`after_disconnect_02.log`及`remote_fixed/`。

运行日志仍有GPIO15路径不存在、校准资料载入失败及调度耗时警告，未据此修改引脚、配置或业务资料。日志处于未连接、空闲状态；未主动发送检测、暂停或归位命令。进程恢复不代表这些仪器条件已解决。短时进程CPU与内存仅保留原采样，不用作资源预算或同板控制验收。

## 配置及上位机范围

以下4份主控配置在恢复前、两次采集及固定证据时逐份指纹相同；归档只保存路径和指纹，没有复制配置内容或凭据。

| 文件 | SHA-256 |
| --- | --- |
| `/userdata/shared/config_linux.ini` | `8b62bdfd565a38dbae844806c1e1ca43f56ad0f93ad5a827118cf21e40cc74fe` |
| `/userdata/shared/consume.ini` | `967dfbbde3771201b4cc33b07e9e4f1e37e9359a737bf90bb66441d840822e25` |
| `/userdata/shared/GL4A_config.cfg` | `9470414d5f95a6b965a5d89e7594fb73ea99fb0743bf2a97bd3bf818d103d86f` |
| `/userdata/shared/number.ini` | `1c6cbb1f065c8eaf0a7701613ea2209320fdd604fb56371e93bf25795aafe116` |

上位机没有启动，没有切换版本、改源码或修改其配置和数据库；`current`仍指向`7c4abe3062ead1f5b6fc6fd495b708be5f11436504bba5e900b6ccb2c4f5d5f9`。本轮没有把主控配置`config_linux.ini`当作上位机的`runtime/data/config.ini`。

## 后续查看与管理

在板B用`systemctl status sn100-original-controller-board-b.service`查看服务；用`tail -f /home/rkshare/programs/aca_flow_test/runs/correct_05/live_20261102_231853/program.log`查看实时输出。关闭查看窗口只结束查看，不结束服务。确需停止时用`systemctl stop sn100-original-controller-board-b.service`；停后才可重新通过原脚本启动，禁止另开一份主控。此处只提供管理方法，本轮未执行停止。

本次服务定义在`/run/systemd/transient/`，重启板卡后不自动恢复；原脚本及原正确程序的正式恢复输入仍沿用[既有归档](../../../../07_backups/sn100_host/board_b/aca_flow_test_20261102/correct_program_1735/README.md)，此次未覆盖系统或原配置，故不额外复制整机或业务库备份。

`remote_fixed/SHA256SUMS`的16份板端固定文件下载后逐项核对通过；本目录`SHA256SUMS`绑定本轮全部证据。移植记录和总计划由整合会话统一接续更新。之后的上位机连接、登录权限、状态闪烁和真实仪器操作仍按获准范围分别验收。
