# 板A本次新程序启动、登录与正常关闭

2026-11-04，本次实际程序版本88ac35f775117aeb9658478ee900006e6aaad2259fa86656ede8462a516d7c74。先严格核主机公钥、序列号3bf8222fe1417f6a及同次主控进程，再用原七份配置和原配套主控在EGLFS显示启动；本目录只记录本次运行，不代替样本按钮、完整LIS、急诊或真实仪器功能验收。

用户看到登录窗口。初次20秒软关闭及一次正常关闭请求被登录窗口拦截，原件保持；用户随后明确已完成登录，再次正常关闭退出0、CLEAN_SHUTDOWN=1，各存储关闭、THREADS_STOPPED true、显示回到tty1。没有强杀、模拟资料或另启主控。七份配置指纹前后相同，主控4426/start_ticks=196590和5000监听保留。

run_files保存12份白名单原始输出，逐项与板端SHA一致；不收集账户库、密码、运行目录中的其他文件。实际TX/RX见app.log，汇总见communication_summary.json；resources.tsv仅为本次短时原始采样，不是资源门槛或长稳通过。新包另存板A独立release，原current保持0b0b9378。匹配程序/697源在07_backups/sn100_host/board_a/formal_merge_20261104_01/application。

统一说明见[本次移植记录](../../../../02_records/20261022_sn100_host_migration.md#formal-merge-temp-preservation-20261104)。本目录脚本是当次执行原件，带固定进程/路径，不能直接重跑。
