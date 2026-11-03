# 板B按固定目录放置主控程序与配套资料

2026-11-02，用户提供主控代码截图，指出程序使用绝对路径`/userdata/shared/config_linux.ini`，要求把Linux主控流程程序的配套资料放到所需位置。**本轮已建立实际目录`/userdata/shared/`并完整放入原包43个文件，部署及退出后立即核对均一致；17:27再次检查时一份原包旧日志已不存在，其余42份仍一致，原因待核。程序仍启动退出255，尚未打开这个INI配置，也未开放5000端口。**

先通过已核实的SSH主机指纹`SHA256:S4ihaw0V/Z8w7v3VlJOQEFLEH3Hp6nr3KjiUTxanwPg`及硬件序列号`8314b1e8ffd0321f`确认板B。有线`192.168.100.123`连接超时，改用本板已有无线地址`192.168.3.59`连接成功；板内检查显示两个地址仍在。本轮不修改网络设置，不对有线连接作恢复通过的结论。

## 文件怎样放

`config_linux.ini`是普通配置文件，用来保存开关、参数和其他文件的路径；`/userdata/shared/`是目录。运行的是可执行程序，程序再读取配置、数据库和流程文件，不能把它们合并写进INI。

```text
/userdata/shared/
├── ACA_V1.0OffLineD-500
├── config_linux.ini
├── number.ini
├── consume.ini
├── abnormal_Config.csv
├── reagent_Setting.db
├── Channel.db
├── OperationMap.db
├── project/                 项目数据库及配套的-wal、-shm文件
├── public_operation500/     原包的两份流程CSV，原本均为0字节
└── ……                      原包其余文件也保留原名和层级
```

配置中的`reagentSettingFile=/userdata/shared/reagent_Setting.db`已对应到实际文件。截图中的`number600.ini`、`number900.ini`和`number100.ini`由编译时机型条件选择；当前收到的是SN500配套包，保留其中原名`number.ini`，不凭截图把文件改名或替换机型参数。绝对路径从系统根目录开始定位，不随启动时所在目录变化；本轮仍从`/userdata/shared`启动，以兼顾程序其他相对路径。

操作前目标路径不存在，只新增本轮目录和检查日志，没有覆盖旧数据，因此无需备份不存在的旧目录。通过root连接部署，目录归lckfb，普通用户实际运行。没有执行原包的开机、清理或全目录权限修改脚本。此前测试目录仍保留，本轮实际目录不会在测试结束后移除，也没有加入开机启动。

## 实际结果

17:23:26从实际目录限时10秒启动，程序很快自行退出255，没有触发超时。43项文件指纹、配置可读性、程序运行组件检查通过；配置文件指纹为`8b62bdfd565a38dbae844806c1e1ca43f56ad0f93ad5a827118cf21e40cc74fe`，程序仍是原来的`bfb23e400584ee9f3b44eee2680fdd8eff85c51729963510f8612c71926ae33f`，未重新编译。

系统调用记录主进程3770打开`/etc/init.d/key.ini`返回`ENOENT`，随后`exit_group(-1)`；记录中未打开`/userdata/shared/config_linux.ini`，不能把文件已放齐、可读说成程序已读取成功。5000端口未开放。日志还打印`WINDOWS_DEBUG is true, and AL4A_INCLUDE is false`；用户截图路径赋值受`#ifndef WINDOWS_DEBUG`条件控制，这是应核对编译定义的线索，但截图未含后续入口读取代码，不能仅凭该输出认定全部原因。

下一步核对与本程序匹配的`main.cpp`第323行附近读取逻辑、截图未显示的`#else`分支和`WINDOWS_DEBUG`编译定义，确认作者所指版本/启动条件。路径已经具备，不再以缺少`config_linux.ini`解释本次失败，也不预设应补造key.ini。确认持续运行及5000端口后再做上位机状态/版本收发。

本轮未修改上位机源码、数据或连接设置，没有执行其启动/停止操作，没有连接真实仪器。本轮只完成文件部署、运行组件检查和启动失败复现；完整流程、上位机通信、资源占用及长时间运行均未验证。

**17:27收尾检查补充：**`GL4A_LOG/GL4A_Critical/critical_2021-01-01.log`这份原包旧日志已不存在，其他42份原文件仍一致，配置、程序、数据库和流程文件均在。部署时和启动后立即核对的43项通过记录保留，不把它们当作17:27仍全部存在的证据。日志消失原因尚未核清，本轮没有再运行程序或擅自恢复该日志；43份原始输入完整保存在正式恢复包中。

| 文件 | 核对内容 |
| --- | --- |
| [before.log](before.log) | 板B身份、网络、原目标不存在及原端口 |
| [SN500_expected_43.sha256](SN500_expected_43.sha256)、[files_before.log](files_before.log)、[files_after.log](files_after.log) | 43个原文件的指纹及部署/运行后两轮一致结果 |
| [path_check.log](path_check.log)、[dependencies.log](dependencies.log) | 实际目录、配置可读、程序/配置指纹及运行组件 |
| [program.log](program.log)、[syscalls.log](syscalls.log) | 实际输出、主进程打开失败及退出 |
| [result.txt](result.txt)、[listeners_after.log](listeners_after.log)、[processes_after.log](processes_after.log) | 程序退出255、配置未被主程序打开、端口及收尾进程 |
| [final_files.log](final_files.log) | 17:27再次检查：42份一致，一份2021年旧日志不存在；不记为43项全部通过 |
| [deploy_fixed_path_03.sh](deploy_fixed_path_03.sh) | 本轮实际执行脚本；要求目标不存在，不能用于直接覆盖已有运行目录 |
| [fixed_path_03_ssh.log](fixed_path_03_ssh.log)、[fixed_path_03_ssh_exit.txt](fixed_path_03_ssh_exit.txt) | 外层SSH实际退出0，与程序255分开登记 |

本轮原配套输入已归档到[完整包恢复目录](../../../../../07_backups/sn100_host/board_b/aca_flow_test_20261102/full_package/README.md)。恢复或撤回前先核板B身份、实际目录和是否有后续运行数据；撤回范围仅为本轮新建`/userdata/shared`，不要删除整个`/userdata`或其他测试目录。`SHA256SUMS`仅核本轮除README外的证据，首轮及第二轮固定清单保持原样。
