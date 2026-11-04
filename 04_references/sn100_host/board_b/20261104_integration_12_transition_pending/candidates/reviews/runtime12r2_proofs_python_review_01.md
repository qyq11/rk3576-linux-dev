# 新02链运行凭据采集Python首审

结论：01完整Python审查已收口，除Root已安排02补正的三项明确缺口外，未见新的确定阻点。**01保留且不执行，待02对这三项增量复核。** 本报告不把旧a27f的AST检查或仅三行映射当成已独审依据，实际亲读当前291行全部代码。

对象为 `runtime_batch12_02/collect_runtime_proofs_batch12_01.py`，19348字节，起止SHA-256为 `d08fea2553d74b51aca948d7310daa7a50712b49e6473d75a9a9008fd016453d`。本次只读源码和必要已审定义，做AST解析与字节比较，只新增本报告；未导入或执行候选/helper、生成采集脚本、运行测试/模拟/SQL、解析真实产品数据或SSH，也未修改草案。

## 已固定的新LIS helper

新 `collect_runtime_lis_batch12_01.py` 实际19245字节，SHA为 `21805f6c036da483cba5a0787daf67a1001fff568a15321967a130e1af6a744c`。对旧已审57f18c…原字节只映射：core3446→652一处、runtime_01→02两处（CAP及回执transition）、lis_01→lis_02一处，完整结果逐字等新文件；其余逻辑未改。

proof的load_guard先核该长度和SHA，再构造名为 `_sn100_batch12_readonly_guard` 的ModuleType、设置真实helper路径为__file__并加载固定字节。已静态检查helper顶层为stdlib导入、纯常量/路径、类/函数定义和 `if __name__ == '__main__'`，该模块名不满足main守卫，不会隐式执行helper的SSH或解析流程。后续Originals又读取helper并核固定SHA，连接前复核来源未漂；没有接受任意模块路径或未pin源码。

## 01其余Python逻辑

- 七phase逐份读取本轮CAP的原receipt、完整stdout导出log、真实LF0退出和冻结allowed授权，核phase/core/auth SHA、transition、board/serial/boot及该阶段current关系；stop还要求APP_EXIT0/CLEAN_SHUTDOWN1/工具实际0/显示释放。六个后继逐一核四个实际前置record连续，launch/observe另复用固定helper身份核对并要求同一新run/PID/start/旧停摘要和真实观察字段。缺少材料不会转成空成功。
- 本地来源采用普通单链接/无链接路径、读取上限、唯一JSON字段和非法数值拒绝，并在网络前后复核不变。输出限本目录全新单层名字；g.exclusive使用O_EXCL及可用的O_NOFOLLOW，原stdout/stderr/命令/实际退出独占保存。没有删除、覆盖或自动重跑产品。
- 文件白名单只由固定core、真实新run及明确文件名组成，不扫描业务资料或接收任意远端路径。嵌入同一新PID检查函数后，远端脚本在tar前后check_context，读串号/boot/PID/start/UID/exe/SHA/current、原主控/相机/服务/5000和配置/指针，只调用读取及tar命令，没有产品启停、切版、INI/数据库写入。
- SSH前读取固定known_hosts、用ssh-keygen核唯一已知板B指纹，SSH本身严格指定ED25519/别名/known_hosts。原网络输出直接独占落盘，SSH非0按实际值返回；包或字段检查失败保留原件并非0退出，没有补0或把命令不存在改成成功。
- 压缩/解压读取各32MiB门、逐成员上限、固定白名单/完整集合/不重复/普通类型/无PAX或稀疏字段均在；不用extractall，也不直接信tar路径任意写文件。known expected原件逐字比较后才存展开结果。上限是进入读取/展开的界限，网络输出先流式落盘；不据此声称RSS或资源已实测。

这些是对补充凭据工具的静态核对，不替代已pin生成器的阶段判定、Root逐阶段真执行或独立a0ea LIS软件结果解析，也未另造模拟输入/测试。

## 02必须收敛的三项

1. 新run的exit_code/result不存在须在采集前后实核，并把相应事实纳入只读快照，不能仅凭进程仍活就默认无退出封口。
2. 实读old_stop_files九件，逐stop receipt SHA及旧C7干净退出封口核对；远端原oldrun六件纳白名单，已存在的stop stdout/stderr/actual_exit三件补expected，避免重复；activate/launch/observe三后继的stop来源须绑定同一rows.stop。
3. 01第264–268行只核snapshot BEGIN/END唯一且末尾END，随即原文提取，没有解析或校验内部字段/重复键/current、PID/start、core/release、实际两camera/service及七配置三pointer共10条SHA。02须补严格解析并与core/live/原指纹表精确核对，才能把工具0解释为已核现场，而不只是包和marker通过。

以上三项已经由Root分别接收并交作者另存02；01和原件不覆盖。除此之外不扩大为新的防御性管线或业务验收。Root本轮prepare运行事实不由本静态审查补写；新工具当前没有取得收证/长稳/真实仪器或产品退出通过结论。
