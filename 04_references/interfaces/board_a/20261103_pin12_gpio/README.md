# 板A排针12脚GPIO配置与软件验证证据

2026-11-03，板A序列号`3bf8222fe1417f6a`，主机指纹与原板A一致。经已有无线地址`192.168.3.38`操作，沿用已信任主机密钥并保持严格校验；没有向最初返回板B指纹的有线地址写配置。只关闭`GPIO0_B5`的PWM，原五组串口、CAN和显示配置保留。

**实际通过范围：配置编译/合并检查、ARM64工具编译/上传和依赖核对、一次板A重启、GPIO输出低→高→低的内核数字读回、输入模式及读回0。** 未测实际电压、未施加外部高低输入、未接入485自动方向切换或验证485通信。最终GPIO空闲、输入、下拉，没有遗留保持输出的后台进程。

| 文件 | 内容 |
| --- | --- |
| `preflight.sh`、`preflight.log`及`.exit` | 修改前板A身份、启动设置、GPIO及PWM占用、原接口状态；最终只读检查SSH退出0 |
| `configure.sh`、`configure.log`及`.exit` | 写入前比较原设置、核对上传文件、依赖检查、启用新配置及原设置的失败恢复处理；SSH退出0 |
| `reboot.sh`、`reboot.log`及`.exit` | 核对板A及配置指纹后请求一次重启；SSH退出0 |
| `verify.sh`、`verification.log`及`.exit` | 重启编号变化、PWM关闭、低/高/低内核读回、输入模式、原配置指纹及串口/CAN引脚；SSH退出0 |
| `gpio_output_run.log`、`dependencies.log` | 板端实际工具交互及动态库解析原输出 |
| `build.sh`、`build_overlay.log`、`build_gpio.log` | 小配置在系统构建WSL编译，GPIO工具在默认Ubuntu-24.04交叉编译；两个分步构建均退出0，没有在电脑运行功能测试 |
| `build_22_combined_failed.log` | 中间尝试在Ubuntu22.04把两项一起构建时，板端libgpiod依赖较新GLIBC导致链接失败；保留原失败，随后改为上述分步构建，没有部署失败产物 |
| `device_tree_changes.diff` | 与板A实际硬件说明合并后，只有`/pwm@2add1000/status`由`okay`变为`disabled` |
| `deployment_board.sha256`、`deployment_inputs.sha256` | 上传文件、原启动设置及编译源码的指纹 |
| `invoke_board_a.ps1` | 电脑执行入口：只使用已有板A主机密钥校验，不修改认证设置 |
| `SHA256SUMS` | 本目录证据文件的指纹清单，清单自身不列入 |

电脑默认Ubuntu-24.04没有`dtc`，首次配置编译因此停止；随后使用已有工具的系统构建WSL，只做配置编译与合并检查。一次早期只读输出通过PowerShell文本管道时附加了回车，末尾退出1；改为不改变脚本字节的编码传输后重新只读核查退出0。本目录保存后续实际执行的入口和成功输出，板端配置写入发生在这些问题处理之后。

原内核`6.1.99-rt36-tspi`及实时标志1保持。旧启动标识`5d98c436-a8ae-4a42-a4af-7921c20f4182`，新标识`da1d6357-7389-4cd3-8b0c-bc0c92fc52ac`。原串口和CAN配置文件内容保持，不代表本轮重新验证过其实际通信。[完整操作记录](../../../../02_records/20261021_gpio_output.md#board-a-pin12-gpio-20261103) · [匹配恢复输入和配置/工具](../../../../07_backups/interfaces/board_a_pin12_gpio_20261103/README.md)。
