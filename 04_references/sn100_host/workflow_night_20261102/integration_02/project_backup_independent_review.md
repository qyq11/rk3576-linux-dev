# 项目保存前备份：已合入源码独立复核

复核日期：2026-11-02。复核对象为正式`06_programs/sn100_host/projects/original_project_repository.cpp`，当时SHA-256为`7ddf228dbb583657cfdffa6dcc168e2240f2fd6f5fa7fe6d0c841f8ff366f446`。结合`project_backup_candidate/handoff.md`及原Windows保存/复制函数只读核对；没有修改正式源码，没有编译、运行数据库、连接板卡或操作UI。

**在下面已明确的边界内，未发现确定的阻断bug。**这不是实际备份或恢复通过：SQLite运行版本、锁的现场表现、WAL资料、文件持久化与恢复均仍待板B核对。当前实际验证函数名为`verifyProjectBackup`，逐表函数为`backupTableMatches`。

## 写事务与快照连接

`save`在`:965`取得`BEGIN IMMEDIATE`，`:966`核草稿指纹；`:974`调用backupProjectBeforeSave时还没有执行UPDATE。内部snapshotDatabase（`:370`）另建只读Connection，在它自己的连接上执行VACUUM main INTO。不是在已持写事务的原连接上执行VACUUM，因此不能把“VACUUM不能位于同连接事务内”的限制直接套到此处。

只读审查SQLite官方实现可见，INTO路径为源库启动读事务、输出库单独写入；这里从“另一个连接持BEGIN IMMEDIATE”不能推出必然死锁。文档也把INTO与修改源库的普通VACUUM分开。此为实现与调用顺序的逻辑核对，并未证明当前Qt插件所用SQLite及RK文件系统实际运行已通过。[SQLite官方VACUUM说明](https://www.sqlite.org/lang_vacuum.html) · [官方SQLite源码镜像vacuum.c](https://raw.githubusercontent.com/sqlite/sqlite/master/src/vacuum.c)

备份函数返回后`:977`再核源指纹，然后继续原更新/提交；备份失败使用单独backupMessage，只输出FAILED日志，不把backupMessage覆盖成主保存成功结果，也不擅自回滚或跳过原保存。源资料若前后指纹不一致，仍按原冲突保护中止保存。未见“备份成功”代替“参数提交成功”的路径。

## 核对的不只是行数或常见字段

| 检查 | 代码所能证明的范围 |
| --- | --- |
| sqlite_master结构 | `:468`至`:491`逐项比较type、name、tbl_name、sql及NULL状态；含表、索引、视图、触发器和发现到的内部表。rootpage等物理页信息没有比较，交接已说明不是物理逐字节镜像。 |
| 表和列范围 | `:395`至`:429`核列数、列名与顺序，按所有发现到的列生成比较SQL，不仅选择项目界面显示字段；标识符双引号转义，ROWID别名从固定三个候选中选未被用户列遮蔽的一项。 |
| 实际ROWID | `:423`按未遮蔽真实ROWID排序，`:435`至`:439`比较64位值；VACUUM若重排行身份则备份失败，不输出READY。 |
| 类型与完整值 | 每列读取typeof、原值和CAST AS BLOB。integer按64位值、real按数值比较，text/blob按完整字节比较，NULL单独核；空TEXT不会与NULL混同。浮点是数值等价而非IEEE位模式等价，符合交接写法，不能称物理字节保真。 |
| 全部行与读取失败 | 逐行直到两边都结束，核少行、多行及lastError；64表/256结构/4096行/256列是明确上限，超界返回失败，不截断报成功。 |
| 库标记 | `:502`至`:508`比较user_version、application_id和encoding；没有只因quick_check为ok就声称原参数身份完整。 |

WITHOUT ROWID、虚拟表、未知表定义、全部ROWID别名被遮蔽或缺扩展时均保守失败，原保存继续。这些是候选已明示的支持边界，不把它们重新列作“必须放宽才能完成”的bug。真正普通项目文件在该SQLite版本下能否保持ROWID，以及超过边界时实际日志和保存结果，需要板B原资料验证。

## 路径、发布和清理

- `checkRoot`先核许可标记、项目目录及路径中无符号链接；backupProjectBeforeSave再核source位于当前root/project、文件名等于before.originalFileName。目标日期与秒级文件名来自当前日期时间和已核源文件叶名，未发现可由项目显示名称拼出项目根外路径的写入。
- 日期目录会核存在/类型/符号链接，临时目录在该日期目录内由QTemporaryDir独立创建。快照先在临时目录完成，失败时自动清理本次临时目录，不批量删除日期目录或其他备份。
- 数据库和清单目标均先检查存在/符号链接，QFile::rename不覆盖旧文件，同秒冲突返回失败并保留旧版本。未见为了发布而删除已存在备份的路径。
- 数据库已发布而清单rename失败时保留数据库，日志明确它的位置，不输出READY；交接也明确这种不完整发布状态。恢复不能凭这份孤立DB就假定有完整清单，应人工核来源与指纹。现代码没有自动恢复或重试覆盖它。
- backupFingerprint是在独立写日志不存在后按现有fingerprint口径读取；该阶段只有无WAL的单文件，得到的值与这一份DB文件SHA-256一致。清单中的source_fingerprint仍是主文件加WAL的原版本口径，两者用途分开，未声称两份文件物理字节相同。

这里没有引入对抗外部进程恶意替换目录的文件描述符级防护，也没有发现应用自身会并发这样替换路径的调用链；不能仅凭一般性TOCTOU可能性就断言当前正常保存发生越界写入。在线恢复或换资料仍须停相关写入，不能把源码路径检查当成允许在线任意替换的保证。

## 失败结果和恢复范围

原Windows`frmsystem/frmsystemmaterial.cpp:39`至`:72`复制失败只记日志，`:2473`至`:2477`调用后仍保存。当前代码保留这项行为：保存成功和备份成功必须分别看READY/FAILED日志，页面保存成功不是备份通过。

恢复范围只是一份保存前项目DB及该文件自己的所有表；没有复制外部校准库、试剂批组、单位、患者/账户、主控INI或其他项目。交接对此说明准确；本轮未提供也未验证成套业务恢复，恢复后仍须让页面重读并核主控同步。

## 待实际验证，不能借编译补结论

板B需按当次已授权保存范围，核真实Qt/SQLite下两连接锁行为、原文件WAL内容能否完整进入快照、ROWID/类型/所有行检查的实际结果、同秒冲突/磁盘及权限失败后原保存结果、清单SHA与单文件恢复，以及保存耗时和备份增长。缺少条件保留待RK验证；不创建匿名测试库、不运行本机数据库或模拟器来替代。

本次只读复核没有新增构建、Windows运行、RK备份、仪器或恢复通过结论。
