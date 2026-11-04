# 新02链proof02三项增量复核

结论：三项已定缺口已静态收敛，未见新增阻点。旧01继续保留不执行；02可作为Root后续真实运行凭据采集候选，但本报告没有执行它或填任何阶段/采集成功。软件业务结果仍以独立a0ea解析和真实条件为准。

实际02源码SHA-256为 `5d2aa85c7762e1070ba9974af791e40c0dcf0d1126964eac50d935e03f19c47e`，27533字节；delta为 `f3a7ddb024b9d4b2d431c90f28684d31e84a9500485d59a123fc62fff71b685a`，10897字节，逐字等两份真实源码按二行上下文生成的差异。01仍为d08fea…，未覆盖。只读/AST/字节比较，未导入、执行、生成脚本、测试、SQL或SSH，只新增本报告。

1. **真实旧停九件与同一stop链。** 新read_old_stop实读activate授权中的恰九项record，核内容SHA和实际字节数；result必须精确APP_EXIT0/CLEAN_SHUTDOWN1，旧PID/start/exe/boot、两个实际退出原文均对应core里的准确旧C7。九个摘要分别等stop receipt，stop自身旧run/映像/helper/清单及干净退出/显示释放事实也核对。activate、launch、observe均须使用同一九件、稳定意图和rows.stop的receipt/exit/log，三份后继回执也须回指该stop SHA。file_scope只增加旧run六件，现有stop stdout/stderr/actual_exit三项原位置补expected，空stderr也保留精确比较，没有重复登记或混用新run。
2. **新run没有退出封口。** 只读脚本文字新增check_new_run_files，核准确规范新run目录及exit_code/result.txt均不存在、也不是悬空链接。tar前后调用的check_context头尾都核新run封口与同一活进程，已有主控/相机/配置等上下文读取仍保留；没有为了收证停止、重启或清理产品。
3. **实际现场严格解析。** 新read_snapshot在1MiB stderr界限内核唯一BEGIN/END和末尾，再解析原文：单值字段集合、唯一UTC格式及真实日历、core/release/new PID/start/exe/SHA/current、原主控等逐项等固定输入；只允许恰两条且各自准确的camera/3A身份，服务Id/ActiveState/SubState/MainPID四值一致。七配置原文清单须规范且恰七项，再加三个不重叠指针；现场10条SHA路径不允许重复、缺失、多余或改值。未知/重复/不完整字段拒绝，解析失败为非0；原stderr与返回snapshot原文仍按原字节保存，不把仅marker存在写成已核现场。

除这三项关联增量及datetime标准库导入外，没有借本片放宽原逻辑。require、load_guard、record、read_phases、read_bundle完整函数字节与01相同；helper仍实际19245字节/SHA `21805f6c036da483cba5a0787daf67a1001fff568a15321967a130e1af6a744c`。既有固定源码pin、非main动态定义加载、七phase真LF0与连续来源链、SSH已知指纹、严界tar、独占保存/实际错误返回和原件保留继续沿用首审范围。

本次未造模拟输入或执行负向测试，以上判断来自实际分支、调用点和完整差异。Root当前prepare/stop等实际动作由对应控制脚本及真实回执另审另记；proof02审查通过不等于新版已经启动、退出、收证或真实LIS/仪器通过。

收口时Root通报：新02 stop实际返回4/CLOSE_PENDING，正常关闭helper仅调用一次；e54b18核原七配置、三指针和C7进程/current保持，旧run没有exit/result；dfc1d6的原app.log含SHUTDOWN_REQUEST SIGTERM和SHUTDOWN_REJECTED window_guard_or_unavailable。这些为Root提供的现场结果，本次未重新联板核验。本链目前不具备继续activate、launch、observe的真实成功前置，proof02与LIScollector均未执行；保留界面，不重试、强杀或绕过关闭守卫。
