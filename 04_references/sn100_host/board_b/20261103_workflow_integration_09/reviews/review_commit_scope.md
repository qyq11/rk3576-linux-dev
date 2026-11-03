# 本轮单次提交的非SN100范围只读审查

2026-11-03。只读核Git差异、原记录、已有编译/板端日志、源码与恢复包及SHA清单、文件属性；仅写本报告。没有重新编译、执行产品、操作板卡或访问网络，没有运行git add/commit/push，没有删除文件。

结论：下列改动可作为**已经完成的配置、工具和有范围的实板实验阶段成果**纳入用户本轮单次提交。不能写成485业务、电气测量或全部故障验证已经完成。中间失败、原件及未验边界均应随现有记录保存。

## 源码和实际来源对应

| 对象 | 实际差异与已核身份 | 可保存的完成范围 |
| --- | --- | --- |
| `05_tools/tests/gpio/gpio_output.c`及README | 只增加物理12脚→gpio0偏移13/`GPIO0_B5`映射、支持列表与使用说明，原引脚映射保留。当前C源码SHA `168498ceae2c8866e63fe9ee2b5015ab17b8aa3501ae8a2b8822925709833349`，与恢复目录源码及deployment_inputs完全相同。 | Ubuntu-24.04 ARM64交叉编译、上传及板A数字低→高→低读回。不是实测电压。 |
| `06_programs/rk3576_system/configs/uart3_485_gpio_pin12.dts`及系统README一行 | DTS只把`pwm1_6ch_1/status`设disabled；当前SHA `ded2ae81fdd6eb6db455964c3534e8094d21fed107b6d4cf0cf4daa7f35fc763`，与恢复源码、部署清单一致。合并差异仅`/pwm@2add1000/status`一处。 | 小叠加配置编译/合并、板A安装和重启生效；未加入既有整机镜像，板B未套用。 |
| 板A恢复输入 | dtbo SHA `3449ee46cdc47e1777b150a72601064e1d375a690906b58fac53c0a87e4c70d8`；gzip解压后的程序为ARM64 ELF，SHA `6d6905d02687b631af852030340e100c1bdb759c99f7168eb9a79ec6a5ac3585`，与板端部署/构建记录相同。 | 仅受影响启动设置、匹配源码、dtbo和程序恢复范围；实际回退未执行。 |

板A身份是序列`3bf8222fe1417f6a`及ED25519指纹`SHA256:xLQv5sqzTRvKbfD2fNQQLYjCbPN6WOXgkXEoUqV8Yy4`。记录核过重启编号变化、PWM关闭、原串口/CAN/显示配置指纹。后续`set_high.log`确实显示临时`gpio-pin12-output.service` active、输出数字1、退出0；因此早先“输入/下拉”只是配置检查结束时点，之后的当前高电平已由第7.4节承接。

Ubuntu22.04合并构建因GLIBC依赖链接失败、默认WSL缺dtc及早期脚本换行问题均按记录保留；最终分步构建与验证成功不覆盖旧失败，也没有部署失败产物。

## 板B心跳与一次panic实验

`20261103_status_led/`保留厂家SDK的LED设备树原摘录、带原GPL标识的心跳源码，以及板B身份和实际`work`/GPIO0_A0/active-low/heartbeat只读输出。源码解释与软件读取可保存；Power灯完整电路、光学时序、电阻/器件位号仍未核实。

`20261103_panic_led/`有授权/协调范围记录、触发前后boot、原内核参数、七配置摘要、当前08指针、旧与本轮pstore原件、触发及恢复日志和用户实际灯态反馈。它证明一次受控SysRq panic及系统恢复，不能推定应用卡住、全部死机方式、看门狗或SN100故障保护通过。触发连接退出255是崩溃后的SSH中断，不是正常退出0；前后业务未被该实验聊天重开。

## 清单与文件保存规则

本次已逐项重算：状态灯3项、panic13项、板A配置23项、持续高电平3项、有限恢复9项，全部与各自SHA256SUMS一致。状态灯清单有意只列三份源证据，README不在清单；其余四目录无未列普通文件（清单自身除外）。

最初板B GPIO证据缺`-text`规则，core.autocrlf=true可能在以后Windows检出时改变字节。root已新增`04_references/gpio/.gitattributes`，只约束上述两组新证据；本次实际`git check-attr text`已返回unset。板A两组interfaces证据原已有unset；恢复目录的`.gz`和`.dtbo`均实际为LFS filter/diff/merge且text unset。没有要求修改被冻结的根属性文件。

工作源码C、维护README和事项记录仍按普通文本管理；对应已编译源码的原字节另外保存在07_backups并受-text保护。当前`git diff --check`未报空白错误，仅提示普通文本以后可能转为CRLF；不能把普通源的检出换行提示误当固定证据已经损坏。

## 建议精确stage路径

以下是范围建议，不是本报告已执行暂存。不要用整个temp、整个系统工程或整个参考目录替代：

```text
02_records/20261021_gpio_output.md
05_tools/tests/gpio/README.md
05_tools/tests/gpio/gpio_output.c
06_programs/rk3576_system/README.md
06_programs/rk3576_system/configs/uart3_485_gpio_pin12.dts
04_references/gpio/.gitattributes
04_references/gpio/board_b/20261103_status_led/
04_references/gpio/board_b/20261103_panic_led/
04_references/interfaces/board_a/20261103_pin12_gpio/
04_references/interfaces/board_a/20261103_pin12_high/
07_backups/interfaces/board_a_pin12_gpio_20261103/
```

根README及SN100主记录中的协调/实验交还入口可以随总控本轮已审公共文档一起提交，不必为此重复暂存其他无关旧记录。`05_tools/tests/gpio/build.sh`等未修改文件无需额外制造改动。

提交前建议仅校正文档一处：`02_records/20261021_gpio_output.md:3`现在只列第1—5节板A、第6节板B，未覆盖新增7/8节。建议改为“**第1—5、7节为板A，第6、8节为板B；第5、7节含电脑交叉编译。**”不要为此回写带SHA清单的固定原件。

## 明确保留待验、排除在完成结论之外

- 板A12脚实际电压、外部高低输入变化、DE/接收使能接法及有效电平、发送完成后方向切换、485实际通信；板B同项配置及整机镜像集成均未完成。
- 原PWM实际波形、恢复/回退执行、串口/CAN本轮真实通信，以及重启后持续高电平都未验证；临时高电平服务不是开机自启功能。
- Power灯完整供电路径、LED光学测量、其他卡死方式、长期故障恢复和SN100真实仪器验收不在本次GPIO完成范围。
- 不暂存temp编译中间物、可变运行目录、WSL磁盘/整个SDK、认证材料或未审候选；已按本报告核过的正式证据与有限恢复输入不属于可随手删除的中间文件。

以上待验并不要求排除已经真实完成的源码/配置阶段文件；应在提交说明中清楚写明范围，保留已有成功、失败和未验证的事实。
