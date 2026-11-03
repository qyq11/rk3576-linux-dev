# 第09批复核后留屏启动的静态审查

2026-11-03。仅只读审总控留屏辅助、既有启动器及固定SSH派发器。没有运行留屏辅助、产品、编译、网络、板卡或Git写操作；只在电脑提取脚本文本并让bash检查语法，不执行其中命令。

结论：按本报告锁定的最新字节，两处初审阻点已补齐，没有发现新的确定阻点。可以在本轮case和原始回包独审真实退出均0之后生成，再由总控通过固定SSH指纹的board08.py单次派发。实际启动、屏幕和持续运行仍待总控现场结果，不能由本静态结论替代。

## 本次锁定的文本

- `prepare_keep_open09.py` SHA-256：`0db64be3019fe478d77003d98a693d64ca35df7e857ffb3a742d253416ea1dba`。
- `06_programs/sn100_host/shell/rk_board.sh` SHA-256：`e3587bad320003bf1a2ea7b3176bf14d23527ebaae65b60ffb4bd5388d3866d2`。
- Python AST解析通过；纯AST提取最新f-string，使用本轮实际build_complete的发布号/应用SHA和两份本地已归档清单SHA，仅重建文本。未执行顶层helper，未读或生成运行用退出记录。
- 对重建的完整脚本文本执行`bash -n`，实际退出0，文本3452字节；前后helper原字节一致。

## 已补的两处阻点

1. 既有`start`可能在程序曾就绪但迅速正常退出时返回0；`status`在已有exit_code时也会正常打印退出结果，不能用两个命令的0就推断仍在运行。最新helper已增加lckfb身份下的独立存活核对：本次latest_run不同于旧值，没有exit_code，发布号及app.exe匹配，process_matches核启动编号/PID开始时刻/用户/exe，SHELL_READY存在，再对/proc中真实exe核应用SHA。全部成立后才输出FORMAL_PROGRAM_LEFT_RUNNING=1。
2. 旧七文件摘要只能证明旧路径文件没改，不能证明正式入口仍指向它们。最新helper先要求original_project_root、instrument_configuration_file、theme_configuration_file都是常规非链接文件，值严格为既定runtime/data及其config.ini/theme_config.ini，资料根规范路径和原资料标记也须成立；避免指针丢失后走旧本地/匿名初始化分支。随后仍先pin两份清单自身SHA，再核三应用INI和四主控配置，不修改实际配置。

## 其余门槛与调用含义

- helper生成前只读要求本轮run09_revision05.exit和independent_review09_revision05.exit均0；从实际build_complete读取本轮发布号和应用SHA，并以排他创建写keep_open09.sh，不覆盖旧执行物料。
- 远端脚本首先核板B硬件序列，严格核current的平层目录和应用SHA。SSH已知公钥、别名及StrictHostKeyChecking由board08.py派发层固定；不要把这个脚本改成只凭IP的裸SSH调用。
- 启动前逐/proc排除既有主程序、检查器及报告辅助程序；要求唯一原ACA exe身份及其5000监听。留屏脚本没有systemd-run原主控、控制器重启、kill控制器或切版命令，末尾另查原控制器exe仍在。
- 正式启动用runuser -u lckfb调用既有rk_board.sh check/start/status；不是root运行产品。原启动器进一步核不可变发布包、动态库/插件、HDMI/DRM/input/font及现有非交互sudo能力，取得共同lifecycle锁并排除既有图形服务、VT7/DRM占用。它不会为了留屏覆盖现有应用。
- 传给start的时长为0，supervise只在seconds非0时才触发软时限关闭，因而本次没有自动关闭时限。服务托管与SSH连接分开；脚本返回后程序仍由原systemd/openvt/supervise链照管。用户主动关闭、异常退出或系统重启仍可能结束程序，0不代表长稳或自动恢复验收。
- 留屏核对没有调用stop。原始启动/status日志和真实派发退出仍须分别保存；最终暂停前再核实际运行身份，不用本报告预填留屏成功。保持运行的程序不存在最终退出码，不可把尚未退出记为应用退出0。

本报告只写此文件，没有修改正式源码、冻结构建或检查器物料。
