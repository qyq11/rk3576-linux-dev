# 板B：SN500配置生效及主控连接

2026-11-02，板B序列号`8314b1e8ffd0321f`，已核SSH指纹`SHA256:S4ihaw0V/Z8w7v3VlJOQEFLEH3Hp6nr3KjiUTxanwPg`。本轮完成ARM64编译、配置接入、RK实际启动与主控收发、正常关闭重开。随后用户手动操作并关闭上位机，主控继续运行；当前数据保留用户操作后的状态。没有完成真实仪器检测、归位、暂停恢复动作或长期稳定性验收。

**19:06后续状态：**用户确认刚才操作或关闭过上位机。第二轮日志记录注销和SIGINT，应用自报EXIT code=0，但启动封装实际记录APP_EXIT=130、CLEAN_SHUTDOWN=0，不能把这次手动中断记为第一次正常stop的退出0。上位机当前关闭，主控39408继续运行，没有自动重开或覆盖试剂库。当前主题/连接配置不变；原141清单中只有reagent_Setting.db发生变化，42位表46行字段已更新，20位表未变。19:00:28的写入及2次Ur接收见[user_operation_followup.json](user_operation_followup.json)，它不包含数据库内容或认证资料。

登录操作后还发送了2次Up、2次UP[PT]及2次Ur，收到18条UP。主控先返回D-D等其他项目，上位机逐项目核对时报告“仪器返回了其他项目，未确认本次同步”，随后因同步失效拒绝剩余相关数据。完整项目同步尚未通过，不能说只差机械归位。19:06只读复查见[final_followup.log](final_followup.log)；完整期间仍没有Ts/Tp。下文141文件一致及仅Qj的结论限定为18:57至18:58初始未登录检查快照，不扩大到用户后来操作后的状态。

## 配置改了什么

用户的附件实际名称是`theme_config.ini`，截图显示同一文件。RK工作文件逐字节采用完整附件，SHA-256为`029605f35924de646b5402f1e377c93cb26e01952e0daa5fda300fe47f7feed7`。

| 配置 | 原Windows配套文件 | 本次生效值 |
| --- | --- | --- |
| 机型 | SN450 | SN500 |
| 试剂位 | 20 | 42 |
| 普通样本孔 | 10 | 5 |
| 急诊孔 | 3 | 5 |
| 检测通道 | 18 | 24 |
| 报告方式 | 1 | 1，未变 |

医院、公司、颜色及82×82标志尺寸保持附件原值。旧RK原型固定显示的SN100已改为读取配置，连接后显示主控实际返回的SN500。42位盘沿用原42位界面和底图，另有4个SD独立孔，来源见[文件依据](panel_origins.json)。

实际主题路径：`/home/rkshare/programs/sn100_shell/runtime/data/theme_config.ini`。同目录`config.ini`的主通信和日志地址改成127.0.0.1，主端口仍为5000；两个程序都在板B。两份INI七键变化见[逐键清单](configuration_changes.json)，原资料安装和134项配置读取检查见[相邻记录](../20261102_original_files/README.md)。

## 怎样验证

首次包120579b7已能读取四份INI共134项，但读取42位试剂表SD_1原历史剩余量`-12560`时退出3，没有进入就绪。已按原Windows行为允许读取有限的负历史量，空used_ul按amount_uL检查；原库原文保留，任务分配、清洗液用量及扣量仍分别校验。首次失败保存在[rk/failed_first_start](rk/failed_first_start/)，修复源码在`reagents/original_reagent_repository.cpp`。

修复后完整247对象重新交叉编译、运行组件检查、打包及板端文件/动态库核对通过，实际构建退出0。

- 发布：`eb9d9ae6d593fa20155f83f4e1c8455cc694da083c919f7142343187d1515e9b`。
- 主程序SHA-256：`e235167c5691968928b5e18eaad0d43d08bad12d1c7638c59065f4111d2c5af4`。
- 包SHA-256：`81493f022b335e3f43751a6dc34fec0f7f60d38dd31facf00ee3bc9a8af902b4`。
- [RK专项检查](rk/check_final.log)：使用实际原资料、实际通信类和正在运行的主控，17项通过、0项失败、实际退出0。核42位及4个SD孔、5+5孔、24通道、颜色、原负剩余量、版本和空闲状态按钮拒绝；只发Qj，没有Ts或Tp。
- [局部面板截图](rk/check_result_final/reagent_panel_42.png)由RK渲染。它是局部控件检查，不代表完整HDMI界面已由人逐页确认。
- 首次完整运行`20261102_185650_54537`进入SHELL_READY，读取原配置/原库并连接主控；18:57:21正常关闭，实际`APP_EXIT=0`、`CLEAN_SHUTDOWN=1`，见[rk/first](rk/first/)。
- 重开`20261102_185728_56320`进入SHELL_READY，继续连接并收到SN500状态，18:58取证时PID56487保持运行，见[rk/reopened](rk/reopened/)。后来用户操作及手动关闭的结果见本页开头，不覆盖初始快照。
- 两轮实际参数均带`--original-db-root`、`--instrument-control`及正确配置路径，没有匿名会话、匿名种子或模拟服务。两轮141份原工作文件校验均退出0，四INI指纹保持；没有改写原数据库来消除历史负值。

旧构建格式仍保留`--offline`和部分offline日志文字；实际启用的连接参数、TCP连接和收发见上述证据，不能仅凭旧文字判断未连接。旧模拟测试没有在本轮运行。

## 当前连接及未完成事项

主控仍是用户最后更正的`/userdata/shared/ACA_V1.0OffLineD-500`，755、root所有，SHA-256为`ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff`。17:38:07启动的PID39408未重启。18:58最终快照显示上位机和主控的127.0.0.1:5000连接为ESTAB，见[最终状态](rk/final_state.txt)。主控进程自行使用SN450_CPU3标题，实际报文机型是SN500。

主控匹配号20250905016、LinuxNew标记、检测24、普通5/急诊5与上位机识别一致。但主控试剂位字段返回48，附件是42，本次按用户要求保留附件42，未改写主控；原Windows会接收该数量更新，主控48的定义及孔位表仍需核对。主控还先后返回抓手/试剂盘归位超时、仪器归位失败，主状态0、错误类型7、子系统数量0。

开始/恢复沿用Ts，暂停沿用Tp[0]。当前空闲、未就绪且缺完整安全状态，按钮限制保留；正常登录、有效任务、原项目/试剂核对等条件仍需满足。没有发开始、暂停、归位、清报警或项目/试剂写入命令。[接口核对及原Windows依据](interfaces_review.md)说明这些限制。主控原包空流程CSV及校准资料提示也未消除，完整机械流程继续待验。

10.02秒实际连接空闲样本中，上位机CPU为单核27.05%，RSS416044KiB，15线程；主控单核23.65%，RSS6184KiB、6线程；日志分别增加33790和6828字节，见[原始资源样本](connected_resources_10s.txt)。仅有短时观测，没有已确认的资源门槛，也未证明同板实时控制、温度/降频和长期稳定性合格。

## 启动与实时日志

上位机已由用户手动关闭，主控继续运行。以后在板B的root终端使用；程序已经打开时不要重复start：

```bash
runuser -u lckfb -- bash /home/rkshare/programs/sn100_shell/sn100.sh start
runuser -u lckfb -- bash /home/rkshare/programs/sn100_shell/sn100.sh status
runuser -u lckfb -- bash /home/rkshare/programs/sn100_shell/sn100.sh stop
```

主控以后停止后再启动的方法见[主控操作说明](../20261102_aca_test/correct_05/README.md)。`config_linux.ini`存参数和资料路径，程序、流程文件和数据库分别放在`/userdata/shared/`下，不把程序内容写进INI。

电脑已保留`RK3576 Board B - Correct ACA live log`主控实时窗口，并打开`RK3576 Board B - SN500 upper TX RX`上位机收发窗口。上位机查看窗口只跟踪日志，关闭它不停止上位机；主控窗口是前台运行窗口，不要在需要主控继续运行时关闭或按Ctrl+C。

当前上位机日志为`/home/rkshare/programs/sn100_shell/runs/20261102_185728_56320/app.log`。本目录日志是固定快照，不会自动继续更新。程序和匹配源码的有限恢复输入见[正式归档](../../../../07_backups/sn100_host/board_b/sn500_connection_20261102/README.md)，原资料和主控归档各保留自身范围，不互相覆盖。
