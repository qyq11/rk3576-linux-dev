# SN100质控历史接入：447项匹配应用与源码恢复输入

本目录保存release `5fda5cc0f0e948f24d8881b946434c9f483e13ad5df1d083ea0b47327f61a008` 的ARM64应用、私有依赖及严格447项构建/发布输入。冻结SHA为`1486686b182dc8d1da55c52b45c91ea72f1b8c41677676cedf16d72b3fc4f401`。这是经过成员逐项校验的恢复输入，尚未执行RK部署、应用恢复、数据恢复或UUID迁移。

- 应用包：`sn100_shell_5fda5cc0f0e948f24d8881b946434c9f483e13ad5df1d083ea0b47327f61a008.tar.gz`，77948896字节，SHA-256 `3c08bcaa89892ee3d2fb7988643fdcf3c20431616893ac442b4963563aee58c6`。
- 源码包：`sn100_source_5fda5cc0f0e948f24d8881b946434c9f483e13ad5df1d083ea0b47327f61a008.tar.gz`，1087983字节，SHA-256 `76ec806c21a030c050e8da2425efdfb522b573c50b57b1468805df3f0b9495a8`。严格447个普通文件成员，逐项匹配`source_snapshot.json`；不包含全部测试、记录、工具或完整SDK。
- ARM主程序SHA：`2fb421e8d39ac2c9c911601090360fe4bbed215bc40f14529ff1ca614cc6b641`；报告辅助程序SHA：`9a1a9ed8006a7b377600cfcc10c8feb7e8f214645207f9fdf7122838238ce4fc`。两者来自同一次实际完成的ARM构建及依赖检查，包内主辅程序与其逐项对应。
- 原package子进程实际退出：0。`arm64_verification.json`、`object_source_audit.json`、`link_source_map.json`及依赖报告保留原始证明；归档控制器核对对象收据关系，没有重新hash全部对象。

对应WSL主辅编译检查点见[原生构建证据](../../../../04_references/sn100_host/qc_history_integration_20261026/native/handoff.json)，35次累计离线调用见[累计证据](../../../../04_references/sn100_host/qc_history_integration_20261026/cumulative/handoff.json)。这些电脑离线结果不代表Windows原版行为、RK实屏、真实仪器或临床功能已验收。本目录也不提供新的响应、资源或长稳结论。

先确认控制器最终退出0且对应attempt的verification通过、本目录没有`INCOMPLETE.json`，再在新的隔离目录核对本目录`SHA256SUMS`及tar路径、成员与应用内部清单。出现失败标记或缺收据时保留该目录，不从部分归档恢复。不要覆盖当前含未提交修改的工作区，也不要把不同版本的主程序、报告辅助程序、私有库或源码混用。源码解到新的项目副本；Windows、WSL和ARM的对象、库及生成文件分别保存。

正式`shell/rk.sh deploy`会准备并重新构建当前源码，不能把它当作直接恢复本目录旧release的命令。实际写入RK、选择数据根、回退及schema兼容性须另行核对。本应用包仅覆盖该release及所列私有库、插件、字体和模板，不恢复系统Qt、Mesa、libc、内核、驱动或完整开发环境。已有依赖/sysroot只读复用，没有安装或恢复动作；静态依赖闭包不证明板端实际加载通过。

包中不含运行数据库、INI、生产/患者数据、运行账户库、密码、私钥或bootstrap认证输入。匹配源码保留正式工程原有`anonymous=true`账号角色夹具：5行且仅name/role/isdefault字段，没有密码/token；这不是运行账户库恢复。原始历史行没有逐点保存单位证据时仍显示原值、单位未知、相关统计不可用，不能用匿名换算夹具推断旧库单位已恢复。

质控和校准数据的UUID、规范物理路径以及catalog项目/试剂保护登记必须配套。任何应用/数据恢复前先停止所有相关写入程序，再核原路径、数据集身份、schema及项目/批次/试剂关联，包括隐藏历史。换目录、换平台或克隆需要显式迁移核对；不直接清UUID、路径登记或保护hold来绕过冲突。此包没有备份这些运行库，也没有证明跨库数据恢复/迁移可用。

两压缩包须按已核对的Git LFS属性管理；控制器不会自动git add、提交或推送。旧426及更早恢复检查点原样保留。临床质控规则、校准拟合/审核/应用、实际LIS/实体打印、仪器动作、RK运行及认可资源预算继续按各自证据验收。
