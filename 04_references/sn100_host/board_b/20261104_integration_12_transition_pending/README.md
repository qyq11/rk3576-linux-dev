# 第12批SN100换版待处理：部分运行证据

本目录保存2026年11月4日已经发生的准备和关闭待处理结果。不是新版启动或业务通过证据。

- 新02链：metadata、prepare、preflight真实0；唯一正常stop调用返回4/CLOSE_PENDING，外层4。原窗口保护拒绝关闭，C7仍669382/2102645，current仍C7，旧run没有result.txt/exit_code。
- 旧01链：前三阶段真实0，stop79在关闭调用之前因相机身份变化拒绝。两链失败各自保留，没有补停止成功回执。
- 发布6922已完整编译/独立存包并完成平铺准备和普通预检；没有activate/launch/observe、新LIS collection或新版RK软件结果。原C7留屏，不再close/强杀/重试，不启停原主控、相机或3A。
- 当前缺人工操作条件继续待验，不追问密码、不造登录或绕过窗口保护。真实LIS、仪器接受/执行、实体操作、资源和长稳均未由本目录验收。
- candidates中的proof02及双审只是未执行工具源码/静态审查，不是收证结果。没有新程序，故新退出/观察写“未启动、未执行”；旧C7本次未退出，退出结果未评估。

怎样核对：先核SHA256SUMS，再按source_records.json查每件原路径、归档路径、字节和SHA。原exit换行、CAP导出和provenance均保留，不改原字节。源中引用temp路径时由source_records映射回本目录。不要因清单校验通过把部分结果改成七阶段运行通过。

既有固定来源：正式688源表0e5c70ef9d5ac5fa3db34888103b4158783141aac5f90845e225c720d532ac8d；发布69220b2eb393408a0c612b43a0fc954e479e8c450a09a48755b3fd2d43b84f72；包2834aaabfb492f0667e55c2f0a97a01be8fbbd3d100dd273ba46eb2f1ecdc687；主程序fb1d42f5a67b0f3663cb3098947ac69d480d4a6784d790bd055ee3dcd5d8ef91。既有构建/包原件另在第12批固定证据及恢复归档，本目录不重复复制二进制。归档不对正式全树或板端新增观测。

归档只复制白名单原件，没有INI/DB正文、known_hosts、患者正文、凭据或完整app.log。归档后源原件保留，未清理、提交或推送。外层归档命令的真实退出与本目录SHA回读由Root另行记录。

复制原件数：219。完整目录另含README、source_records、叶级.gitattributes与SHA256SUMS共4件。
