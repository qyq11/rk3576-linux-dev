# 完整SN500包在板B直接复测

用户转达主控作者明确说明“不需要key.ini就能启动”，并追问此前结论依据。此前依据为首轮日志main.cpp第323行读取失败及退出255；收到完整包后没有重跑就继续要求补文件，处理不充分。现撤回把“补key.ini”作为前置条件的判断，按当前收到文件复测并核对与作者预期的差异。

2026-11-02 17:16:20，重新确认板B序列号8314b1e8ffd0321f和已核实SSH指纹，上传SN500完整43文件到`/home/rkshare/programs/aca_flow_test/runs/probe_02_full_package/runtime/`。普通用户lckfb直接执行`./ACA_V1.0OffLineD-500`，以新建符号链接提供程序固定的`/userdata/shared`路径；没有使用首轮临时服务限制，没有执行原包的权限修改/清理/开机脚本。设置10秒上限，程序实际很快退出255，没有触发超时。

结果仍是main.cpp第323行读取`/etc/init.d/key.ini`失败。系统调用跟踪工具strace记录主程序打开该文件返回ENOENT（文件不存在），随后exit_group(-1)；外层实际返回255。5000端口未开放。编译时间仍是2026-11-02 16:27:01，文件指纹仍为`bfb23e400584ee9f3b44eee2680fdd8eff85c51729963510f8612c71926ae33f`，与原先部署文件和SN500包内程序一致。

这些证据只说明当前收到的二进制走到了该失败分支；设计上是否应依赖该文件，以及作者所指的不依赖文件版本/启动条件，需要核对。当前不修改二进制、伪造key.ini或把补文件当成唯一解法。

| 文件 | 核对内容 |
| --- | --- |
| [program.log](program.log) | 第11行为本次实际报错，含main.cpp原行号；第4行为编译时间。 |
| [syscalls.log](syscalls.log) | 第59行为主进程26576打开key.ini失败，下一行退出；其他PID是命令子进程，不能混用退出码。 |
| [result.txt](result.txt) | 直接运行返回255；临时路径映射已移除。 |
| [before.log](before.log) / [listeners_after.log](listeners_after.log) | 板B身份、执行方法、完整43文件、输入及程序指纹和端口。 |
| [recheck_full_package.sh](recheck_full_package.sh) | 本次执行脚本，先上传再运行，外层SSH退出0与程序退出255分别记录。 |
| [recheck_full_package.log](recheck_full_package.log) / [recheck_full_package_ssh_exit.txt](recheck_full_package_ssh_exit.txt) | 实际外层输出和退出。 |

测试结束程序已退出，本轮临时创建的`/userdata/shared`链接已移除；上位机文件、连接配置、系统启动配置及原包没有改动，没有连接仪器或建立上位机通信。`SHA256SUMS`仅核对本次新证据，不更改首轮固定清单。
