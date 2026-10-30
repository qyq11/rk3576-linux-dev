# Windows原配置与DB到RK的逐文件核对清单

实际核查：**2026-10-27 19:19:23（北京时间）**。对象是原Windows发行目录`bin/`，不是整个开发工程或IDE配置。

**文件复制已全部核对通过：39/39存在、39/39内容一致、缺失0。业务接入尚未全部完成。**

后续小项更新：医院原库保存修复在RK独立后台已74/0、退出0；未发布到当前主程序，所以本表的部署版使用阶段保持原核查时点。原接入窗口已按用户要求收尾后停止，[结果与新窗口交接](../../02_records/20261022_sn100_host_migration.md#hospital-save-fix-stop-20261027)。

其中包含29份DB、4份INI、2份CFG、2份容量JSON、1份告警CSV和1份报表XML；除模板外的配置/规则共9份。原数据库只计算文件指纹，没有查询或导出患者、口令等记录。

板上已部署版本为`f8c6e73f`。本次核查时**没有检测到`sn100_shell`主窗口进程**；本次没有启动、停止或更新程序。

## 先核对两个根目录

Windows原文件根目录：

```text
C:\Users\52400\Desktop\internship\seerain\20260907_RK3576_Linux\06_programs\sn100_host_reference\Seerain450_PC\bin
```

RK板上的原资料根目录：

```text
/home/rkshare/programs/sn100_shell/runtime/data
```

程序部署根目录为`/home/rkshare/programs/sn100_shell`；当前可执行文件从`/home/rkshare/programs/sn100_shell/current/sn100_shell`进入。数据根不是`releases/`，也不是已清理的临时`sn100_original/20261027_01`。

下表的Windows文件可点击定位，链接是完整Windows路径；RK栏逐行给出完整路径。两边保持相同相对目录、文件名、空格和大小写。`SHA-256`是文件内容指纹，两次板端计算均稳定，并与原Windows文件相同；39项均没有发现SQLite写日志边文件。

## 怎样理解接入状态

- **项目/单位主业务已接入**：已部署版能在项目页面直接读写这些原库，并有本批RK操作证据；未知算法参数仍保留，检测算法本轮暂缓。
- **有限读取或关联核对**：仅用于候选、引用保护等，不表示对应业务页面已完整改用原库。
- **兼容读取**：读出的键和类型/值一致，不表示所有开关已经应用。
- **仅部署或尚未完整接入**：文件在板上，但对应原业务还未接入当前部署版；新后台或源码单独通过的检查另外注明。

| 当前部署版使用阶段 | 文件数 | 还需注意什么 |
| --- | --- | --- |
| 项目配置读写已接入 | 8 | 7份项目库和1份单位库；未知算法键只读，带外部关联的完整复制仍未完成。 |
| 项目页只读取候选资料 | 1 | 默认试剂资料；不表示试剂维护页已经改用原库。 |
| 关联检查按需读取 | 8 | 有项目引用保护代码；本次文件核查没有逐份触发读取。 |
| INI读取兼容，尚未全部应用 | 4 | 134个有效配置项读取一致；所有开关和页面仍需实际接入。 |
| 仅部署 | 18 | 含原外部报表模板；文件存在不表示对应功能已接入。 |

共同运行依据见[原库实施记录](../../02_records/20261022_sn100_host_migration.md#original-db-rk-20261027)，逐文件机器核对结果见[verification.json](../../04_references/sn100_host/original_files_rk_checklist_20261027/verification.json)。以下状态仅代表本次核查时点，后续发布要重新核对。

## 系统资料：7份DB

### 2026-10-28接续源码的页面接线（尚未发布）

下表说明指定`--original-db-root`时的新源码去哪里读写。当前部署仍按上方f8c清单理解；编译、RK页面和真实仪器结果分别补入同一移植记录，不能把此表当作运行通过。

| 页面或功能 | 原资料落点 | 本轮状态与待验 |
| --- | --- | --- |
| 医院名称、五类字典 | `db/userSet/hospital_Information.db`；名称读根`config.ini`，缺键时读`theme_config.ini`，保存只写`config.ini` | 同批设置专项RK11/0，核名称页面与共用配置的保存/取消/存储关闭重开及冲突恢复；历史74/0保留范围，五类字典实际main内操作另验。 |
| 账户、登录和权限 | `db/userSet/user_Information.db`、`db/systemInfo/user.db`、`config_Debug.ini`的`login/`键 | 新入口按原配置建立有效role0，实际main匿名根两次启动关闭正常；不新建演示账户。原口令登录和完整权限分支另验。 |
| 历史查询和原始结果 | `result/results.db`及`project/`、原单位库，模板`resource/report_template/report.xml` | 原历史专项RK14/0，正式历史页读原行、病人字段保存重开、原参数及外部XML的两份PDF已核；真正main打印按钮、实体打印和完整分支另验。 |
| 每日、每周、每月维护记录 | `db/userSet/maintain.db` | 新入口直接打开原表，待RK页面；真实维护动作另验。 |
| 告警历史 | `db/userSet/abnormal_Log.db` | 原模式默认路径已改为原文件大小写；实时告警与复位动作另行接入。 |
| 原项目与单位 | `project/*.db`、`db/systemInfo/option_Unit.db` | 已发布项目页结果保持原范围；新样本/试剂快照保留原编号，待整合RK。 |
| 试剂目录、瓶型和孔位 | 默认`db/userSet/reagent_Information.db`、`db/systemInfo/reagent_Bottle_Type.db`、`db/userSet/reagent_Setting.db` | 正式孔位页清孔和正常有效期保存/取消、原Us99与对应Ur、编辑器重开在RK通过；批组选项仍有Windows差异待修，真实仪器另验。厂家/机型变体没有擅自替换。 |
| 样本架、组合和任务 | 原样本后台按原目录读配置和项目，任务与返回保存到原`result/` | 原编号与主窗桥接已接。正常等待异步读完后，正式页面录入S01/17/PT再开始/暂停/恢复及原曲线待计算保存，RK offscreen6/0、EGLFS三个场景5/0；真实仪器/完整任务分支另验。未猜造`sample_setting.db`。 |
| 打印机本地选择 | 根`config.ini`的`printer/name` | 同批设置专项RK11/0核页面及共享键，实际main两次正常启动；不等于main内修改后整进程重开或实际出纸。 |
| LIS本地设置（实验室信息系统） | 根`config.ini`的`lis/`键 | 同批设置专项RK11/0核原枚举、当前方式保存及其他键保留、冲突恢复；真实收发另验。 |
| 质控、校准 | 原`result/quality.db`、`result/quality_Data.db`、`result/calibration.db`及原ISI/试剂资料 | 新主入口已改原初始化、原项目编号与已提交快照并接原权限，新布局ARM64编译通过。首轮RK4/3退出3，原资料读取和校准取消/保存/冲突恢复已核，错误测试假设与弹窗等待修正后驱动只编译；用户要求后续再测，不称整组完成。 |
| 主题、日志、通信显示过滤和容量配置 | 四份INI、两份CFG及两份容量JSON | 兼容读取不等于全部应用；发送/接收过滤独立专项RK49/0，不表示正式通信窗显示和其余配置全部通过。算法继续暂缓，参数及原始曲线保留。 |

| 手工确认 | 序号 | 用途 | Windows原文件 | RK板上实际完整路径 | 文件内容 | 当前部署版的业务使用 |
| --- | --- | --- | --- | --- | --- | --- |
| ☐ | 01 | 原检测、预热和缓存通道及启用状态 | [db/systemInfo/channel.db](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/db/systemInfo/channel.db>) | `/home/rkshare/programs/sn100_shell/runtime/data/db/systemInfo/channel.db` | 相同 | 仅部署，业务尚未使用 |
| ☐ | 02 | 原单位名称、结果类型及换算倍率 | [db/systemInfo/option_Unit.db](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/db/systemInfo/option_Unit.db>) | `/home/rkshare/programs/sn100_shell/runtime/data/db/systemInfo/option_Unit.db` | 相同 | 项目配置读写已接入 |
| ☐ | 03 | 默认原试剂瓶型名称及瓶型参数 | [db/systemInfo/reagent_Bottle_Type.db](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/db/systemInfo/reagent_Bottle_Type.db>) | `/home/rkshare/programs/sn100_shell/runtime/data/db/systemInfo/reagent_Bottle_Type.db` | 相同 | 仅部署，业务尚未使用 |
| ☐ | 04 | 原命名瓶型变体，不能按450文件名推定SN100选择 | [db/systemInfo/reagent_Bottle_Type_450.db](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/db/systemInfo/reagent_Bottle_Type_450.db>) | `/home/rkshare/programs/sn100_shell/runtime/data/db/systemInfo/reagent_Bottle_Type_450.db` | 相同 | 仅部署，业务尚未使用 |
| ☐ | 05 | 原命名瓶型变体，不能按600文件名推定SN100选择 | [db/systemInfo/reagent_Bottle_Type_600.db](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/db/systemInfo/reagent_Bottle_Type_600.db>) | `/home/rkshare/programs/sn100_shell/runtime/data/db/systemInfo/reagent_Bottle_Type_600.db` | 相同 | 仅部署，业务尚未使用 |
| ☐ | 06 | 原结果状态位及中英文提示字典 | [db/systemInfo/result_Check.db](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/db/systemInfo/result_Check.db>) | `/home/rkshare/programs/sn100_shell/runtime/data/db/systemInfo/result_Check.db` | 相同 | 仅部署，业务尚未使用 |
| ☐ | 07 | 原角色名称及权限字典，不是账户口令表 | [db/systemInfo/user.db](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/db/systemInfo/user.db>) | `/home/rkshare/programs/sn100_shell/runtime/data/db/systemInfo/user.db` | 相同 | 仅部署，业务尚未使用 |

## 使用资料：11份DB

| 手工确认 | 序号 | 用途 | Windows原文件 | RK板上实际完整路径 | 文件内容 | 当前部署版的业务使用 |
| --- | --- | --- | --- | --- | --- | --- |
| ☐ | 08 | 原历史报警事件及处理文本 | [db/userSet/abnormal_Log.db](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/db/userSet/abnormal_Log.db>) | `/home/rkshare/programs/sn100_shell/runtime/data/db/userSet/abnormal_Log.db` | 相同 | 仅部署，业务尚未使用 |
| ☐ | 09 | 原科室、送检者等常用字典，医院名称另在INI中 | [db/userSet/hospital_Information.db](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/db/userSet/hospital_Information.db>) | `/home/rkshare/programs/sn100_shell/runtime/data/db/userSet/hospital_Information.db` | 相同 | 仅部署，业务尚未使用 |
| ☐ | 10 | 原批组ISI及正常参考值文本 | [db/userSet/isi_Value.db](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/db/userSet/isi_Value.db>) | `/home/rkshare/programs/sn100_shell/runtime/data/db/userSet/isi_Value.db` | 相同 | 仅部署，业务尚未使用 |
| ☐ | 11 | 原每日维护记录及动作完成状态 | [db/userSet/maintain.db](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/db/userSet/maintain.db>) | `/home/rkshare/programs/sn100_shell/runtime/data/db/userSet/maintain.db` | 相同 | 仅部署，业务尚未使用 |
| ☐ | 12 | 默认试剂目录、原孔位资料和批组 | [db/userSet/reagent_Information.db](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/db/userSet/reagent_Information.db>) | `/home/rkshare/programs/sn100_shell/runtime/data/db/userSet/reagent_Information.db` | 相同 | 项目页只读取候选资料 |
| ☐ | 13 | 按原文件名保留的试剂资料变体及批组引用 | [db/userSet/reagent_Information_hongkui.db](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/db/userSet/reagent_Information_hongkui.db>) | `/home/rkshare/programs/sn100_shell/runtime/data/db/userSet/reagent_Information_hongkui.db` | 相同 | 仅用于关联检查，未逐份取运行证据 |
| ☐ | 14 | 按原文件名保留的试剂资料变体及批组引用 | [db/userSet/reagent_Information_liangchen.db](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/db/userSet/reagent_Information_liangchen.db>) | `/home/rkshare/programs/sn100_shell/runtime/data/db/userSet/reagent_Information_liangchen.db` | 相同 | 仅用于关联检查，未逐份取运行证据 |
| ☐ | 15 | 按原文件名保留的试剂资料变体及批组引用 | [db/userSet/reagent_Information_seerain.db](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/db/userSet/reagent_Information_seerain.db>) | `/home/rkshare/programs/sn100_shell/runtime/data/db/userSet/reagent_Information_seerain.db` | 相同 | 仅用于关联检查，未逐份取运行证据 |
| ☐ | 16 | 原使用孔位、分配、余量及条码等试剂设置 | [db/userSet/reagent_Setting.db](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/db/userSet/reagent_Setting.db>) | `/home/rkshare/programs/sn100_shell/runtime/data/db/userSet/reagent_Setting.db` | 相同 | 仅部署，业务尚未使用 |
| ☐ | 17 | 原项目组合、项目名称顺序及重复项目 | [db/userSet/sample_Combination.db](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/db/userSet/sample_Combination.db>) | `/home/rkshare/programs/sn100_shell/runtime/data/db/userSet/sample_Combination.db` | 相同 | 仅用于关联检查，未逐份取运行证据 |
| ☐ | 18 | 原账户公开信息及口令记录，本审计不读记录 | [db/userSet/user_Information.db](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/db/userSet/user_Information.db>) | `/home/rkshare/programs/sn100_shell/runtime/data/db/userSet/user_Information.db` | 相同 | 仅部署，业务尚未使用 |

## 检测项目：7份DB

| 手工确认 | 序号 | 用途 | Windows原文件 | RK板上实际完整路径 | 文件内容 | 当前部署版的业务使用 |
| --- | --- | --- | --- | --- | --- | --- |
| ☐ | 19 | PT项目基本信息、稀释/试剂步骤、结果格式、质控及原算法参数 | [project/1 PT.db](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/project/1 PT.db>) | `/home/rkshare/programs/sn100_shell/runtime/data/project/1 PT.db` | 相同 | 项目配置读写已接入 |
| ☐ | 20 | APTT项目基本信息、稀释/试剂步骤、结果格式、质控及原算法参数 | [project/2 APTT.db](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/project/2 APTT.db>) | `/home/rkshare/programs/sn100_shell/runtime/data/project/2 APTT.db` | 相同 | 项目配置读写已接入 |
| ☐ | 21 | TT项目基本信息、稀释/试剂步骤、结果格式、质控及原算法参数 | [project/3 TT.db](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/project/3 TT.db>) | `/home/rkshare/programs/sn100_shell/runtime/data/project/3 TT.db` | 相同 | 项目配置读写已接入 |
| ☐ | 22 | FIB项目基本信息、稀释/试剂步骤、结果格式、质控及原算法参数 | [project/4 FIB.db](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/project/4 FIB.db>) | `/home/rkshare/programs/sn100_shell/runtime/data/project/4 FIB.db` | 相同 | 项目配置读写已接入 |
| ☐ | 23 | D-D项目基本信息、稀释/试剂步骤、结果格式、质控及原算法参数 | [project/5 D-D.db](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/project/5 D-D.db>) | `/home/rkshare/programs/sn100_shell/runtime/data/project/5 D-D.db` | 相同 | 项目配置读写已接入 |
| ☐ | 24 | FDP项目基本信息、稀释/试剂步骤、结果格式、质控及原算法参数 | [project/6 FDP.db](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/project/6 FDP.db>) | `/home/rkshare/programs/sn100_shell/runtime/data/project/6 FDP.db` | 相同 | 项目配置读写已接入 |
| ☐ | 25 | AT-III项目基本信息、稀释/试剂步骤、结果格式、质控及原算法参数 | [project/7 AT-III.db](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/project/7 AT-III.db>) | `/home/rkshare/programs/sn100_shell/runtime/data/project/7 AT-III.db` | 相同 | 项目配置读写已接入 |

## 结果、校准和质控：4份DB

| 手工确认 | 序号 | 用途 | Windows原文件 | RK板上实际完整路径 | 文件内容 | 当前部署版的业务使用 |
| --- | --- | --- | --- | --- | --- | --- |
| ☐ | 26 | 原校准点、审核、批组应用关系及编号计数 | [result/calibration.db](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/result/calibration.db>) | `/home/rkshare/programs/sn100_shell/runtime/data/result/calibration.db` | 相同 | 仅用于关联检查，未逐份取运行证据 |
| ☐ | 27 | 原质控品、厂家、项目、批次及规则 | [result/quality.db](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/result/quality.db>) | `/home/rkshare/programs/sn100_shell/runtime/data/result/quality.db` | 相同 | 仅用于关联检查，未逐份取运行证据 |
| ☐ | 28 | 原质控每日结果历史，与质控配置库分开 | [result/quality_Data.db](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/result/quality_Data.db>) | `/home/rkshare/programs/sn100_shell/runtime/data/result/quality_Data.db` | 相同 | 仅用于关联检查，未逐份取运行证据 |
| ☐ | 29 | 原患者报告、六种结果原字符串、表头和任务计数 | [result/results.db](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/result/results.db>) | `/home/rkshare/programs/sn100_shell/runtime/data/result/results.db` | 相同 | 仅用于关联检查，未逐份取运行证据 |

## 原配置和规则：9份文件

| 手工确认 | 序号 | 用途 | Windows原文件 | RK板上实际完整路径 | 文件内容 | 当前部署版的业务使用 |
| --- | --- | --- | --- | --- | --- | --- |
| ☐ | 30 | 旧容量校准控制点、面型和针偏移修正参数 | [calibrationFile/ReagentAmountCalibration.ini](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/calibrationFile/ReagentAmountCalibration.ini>) | `/home/rkshare/programs/sn100_shell/runtime/data/calibrationFile/ReagentAmountCalibration.ini` | 相同 | 读取兼容；尚未全部应用 |
| ☐ | 31 | 原容量记录命名变体，来源与常规文件同字节 | [calibrationFile/ReagentAmount_450 - TJ.json](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/calibrationFile/ReagentAmount_450 - TJ.json>) | `/home/rkshare/programs/sn100_shell/runtime/data/calibrationFile/ReagentAmount_450 - TJ.json` | 相同 | 仅部署，业务尚未使用 |
| ☐ | 32 | 原共用容量点表、基准面和针偏移记录 | [calibrationFile/ReagentAmount_450.json](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/calibrationFile/ReagentAmount_450.json>) | `/home/rkshare/programs/sn100_shell/runtime/data/calibrationFile/ReagentAmount_450.json` | 相同 | 仅部署，业务尚未使用 |
| ☐ | 33 | 原全局工作、医院、打印、LIS和通信设置，不展示值 | [config.ini](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/config.ini>) | `/home/rkshare/programs/sn100_shell/runtime/data/config.ini` | 相同 | 读取兼容；尚未全部应用 |
| ☐ | 34 | 原默认登录角色、调试、重连、模拟结果和通信显示过滤 | [config_Debug.ini](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/config_Debug.ini>) | `/home/rkshare/programs/sn100_shell/runtime/data/config_Debug.ini` | 相同 | 读取兼容；尚未全部应用 |
| ☐ | 35 | 原报警编号、中文说明、措施及启动/运行限制规则 | [db/systemInfo/abnormal_Config.csv](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/db/systemInfo/abnormal_Config.csv>) | `/home/rkshare/programs/sn100_shell/runtime/data/db/systemInfo/abnormal_Config.csv` | 相同 | 仅部署，业务尚未使用 |
| ☐ | 36 | 原算法项目参数配置，加载参数不等于执行算法 | [lib/config/AL4A_config.cfg](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/lib/config/AL4A_config.cfg>) | `/home/rkshare/programs/sn100_shell/runtime/data/lib/config/AL4A_config.cfg` | 相同 | 仅部署，业务尚未使用 |
| ☐ | 37 | 原日志组件、分类及日志级别开关 | [lib/config/GL4A_config.cfg](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/lib/config/GL4A_config.cfg>) | `/home/rkshare/programs/sn100_shell/runtime/data/lib/config/GL4A_config.cfg` | 相同 | 仅部署，业务尚未使用 |
| ☐ | 38 | 原主题颜色、名称、机型、孔位、通道和报告选项 | [theme_config.ini](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/theme_config.ini>) | `/home/rkshare/programs/sn100_shell/runtime/data/theme_config.ini` | 相同 | 读取兼容；尚未全部应用 |

## 原报表模板：1份XML

| 手工确认 | 序号 | 用途 | Windows原文件 | RK板上实际完整路径 | 文件内容 | 当前部署版的业务使用 |
| --- | --- | --- | --- | --- | --- | --- |
| ☐ | 39 | 原Windows外部报表布局和变量模板 | [resource/report_template/report.xml](<C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host_reference/Seerain450_PC/bin/resource/report_template/report.xml>) | `/home/rkshare/programs/sn100_shell/runtime/data/resource/report_template/report.xml` | 相同 | 仅部署；当前使用其他模板 |

## 新代码和独立检查，不能算作已部署版全部接入

下表说明已经写出的新代码实际验证到哪一步，不增加当前主程序已接入的范围。逐文件的详细依据保存在[verification.json](../../04_references/sn100_host/original_files_rk_checklist_20261027/verification.json)。其中“46/0”等表示该组46项通过、0项失败，包含初始化和收尾检查，不是46项完整业务。

| 文件或范围 | 当前开发与验证阶段 |
| --- | --- |
| `project/*.db`和`option_Unit.db` | 原库读写代码90项、实际项目页面114项在RK通过，正式启动、关闭和重开已核对。没有逐一改写全部原字段；未知算法键只读，带外部关联的完整复制仍待完成。 |
| 四份INI | 原134个有效键的名称、类型和值兼容读取已在RK核对；新的设置读取代码在RK独立检查46/0。正式页面尚未全部使用这些设置；通信显示过滤还发现与Windows规则不一致，需修复并在RK核对。 |
| `channel.db`、`result_Check.db`、`user.db`及告警CSV | 新代码在RK独立检查46/0中读取原资料，并用匿名副本核对通道、提示、权限和启动限制。当前主程序和真实设备流程尚未使用这些新读取结果。 |
| 默认试剂资料、设置、瓶型及`isi_Value.db` | 新试剂后台在RK用匿名原结构检查10/0，覆盖余量、孔位、条码、瓶型、批组和失败回退；尚未接入当前正式主页面。两份450/600瓶型变体和三份厂家资料的切换维护没有对应运行证据，SN100选择不能只凭文件名。 |
| 两份容量JSON及`ReagentAmountCalibration.ini` | 新读取、换算和匿名保存已有RK独立检查，常规JSON原文件只读。带` - TJ`的JSON未按该路径单独运行；正式容量页面和机械动作尚未接入，SN100实际选用与容量仍待核。 |
| `sample_Combination.db`及`results.db` | 原结构的样本/结果后台在RK用匿名资料检查10/0；历史后台另9/0，核对原结果字符串、未知项目、资料保存重开、表头和导出。当前主程序的样本和历史入口仍未接入这些新后台。 |
| 医院、账户、维护和告警历史4份DB | 原结构的读取保存代码及检查程序已完成ARM64交叉编译，尚未在RK执行新检查，当前主入口仍使用原来的独立资料目录。不能拿旧独立库的结果替代。 |
| `result/calibration.db`、`quality.db`、`quality_Data.db`及校准的ISI读写 | 新代码及检查程序已完成ARM64交叉编译，尚未在RK运行；原记录保存、审核和应用等待接入。检测算法按用户要求本轮暂缓。 |
| 两份CFG | 新代码在RK独立检查46/0中核对算法参数读取及Linux日志开关、中文日志输出；尚未接入当前主程序完整业务。读取算法参数不表示执行了算法。 |
| 原外部报表XML | 原样复制；当前程序使用另一份内嵌模板。原模板的字段、布局和实际打印一致性尚未验收。 |

## 哪些文件或目录没有

**本清单39份运行资料，RK缺失文件为0。** 两份容量JSON虽内容相同，仍各自保留，未合并；450/600瓶型和厂家资料也没有删除或改名。当前机型为用户确认的SN100，保留其他资料不代表已按它们配置当前仪器。

原Windows下还有3个空的输出目录，本次板端均未创建。它们原本没有文件，不计入39项“缺失文件”；目录创建和输出功能仍需接入时核对，不能称整个目录树已经逐项复刻。

| 原相对目录 | 本次RK状态 | 核对含义 |
| --- | --- | --- |
| `dectFile/` | 未创建 | 原目录为空；不是丢失了原文件 |
| `calibrationFile/face/` | 未创建 | 原目录为空；不是丢失了原文件 |
| `calibrationFile/vol/` | 未创建 | 原目录为空；不是丢失了原文件 |

此外要区分原资料本来没有的对象：当前原发行包未提供`ReagentAmount_600.json`或SN100专名的容量JSON，SN100与共用容量文件的实际匹配仍需依据现场配置/设备确认，不能凭文件名猜测；未发现`sample_setting.db`，原样本架主要在内存中准备，组合保存在`sample_Combination.db`，不能自行造同名库。检测算法及其Linux实现已由用户明确放到后续，不作为本轮文件复制的缺失项。

**报表XML另有差异：**原`resource/report_template/report.xml`已复制且指纹一致，但当前程序使用内嵌`:/history_report/report.xml`，对应工程的`history/templates/report.xml`；两者字节不同。内嵌报告曾通过的检查，不能替代这份外部原模板的接入验收。

## 你可以怎样复核

- [x] 已按Windows发行目录重新枚举39项，核对与既有原清单一致。
- [x] 已通过SSH在RK逐文件检查路径、普通文件属性、大小和两次内容指纹，39项全相同。
- [x] 已核对实际部署版本和原资料根目录，未修改原DB或配置。
- [ ] 人工查看上述文件夹与文件名，并在表格“手工确认”栏标记。
- [ ] 对照“当前部署版的业务使用”，逐业务检查读取、允许编辑、保存、取消和重开；不能只因文件存在就勾为功能通过。

需要再次核指纹时，在Windows先进入WSL，然后执行下列只读命令。它不复制或覆盖文件。

```powershell
wsl -d Ubuntu-24.04 -u qyq
```

```bash
cd /mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
ssh -o BatchMode=yes lckfb@192.168.100.123 'cd /home/rkshare/programs/sn100_shell/runtime/data && sha256sum -c -' < 04_references/sn100_host/original_files_rk_checklist_20261027/windows_files.SHA256SUMS
```

正常应得到39行`OK`。以后在程序里正常保存过配置后，指纹可能变化，需先核对是否为预期保存；不要为了让清单通过而覆盖已保存的资料。

完整指纹：[Windows原件](../../04_references/sn100_host/original_files_rk_checklist_20261027/windows_files.SHA256SUMS)、[本次板端文件](../../04_references/sn100_host/original_files_rk_checklist_20261027/rk_files.SHA256SUMS)。实际SSH输出和当前版本身份均保存在同一证据目录。
