# 第八批构建清单修订：工具合同审阅

2026-11-03。本报告是实现者对新工具的静态合同核对及指定小文件落盘检查；未运行晋级主流程。**当前工具已可交根绑定最终审查；实际单片回演、正式检查/应用、全量ARM64构建均未由本工具执行。**未知最终审查SHA不写假值，必须在命令行显式给出。

固定实际脚本：`promote_build_input_fix.py`，30222字节，SHA-256 `89edb4a69aace219e5eb759b0f74f6ade13502275ce558069453c853a772f260`。本报告冻结后不再修改该脚本。

## 本次只做什么

原五片已完成的正式结果由父记录继承，本次只补第6片 `06_programs/sn100_host/shell/build.sh`。先核父记录和正式674份旧最终值，再新建独立目录，从正式674份复制原字节，仅普通检查/应用第6片；完整后表与已审候选逐字节相同后，才在正式目录普通检查/应用第6片。代码中只有480行的 `apply_one(preflight,row)` 与497行的 `apply_one(ROOT,row)` 两处调用；`row` 均明确取 `review['patches'][5]`，没有重新正式应用旧五片的循环。

原五片的完成证据固定为：父progress `95b5c41fe17c4220ca75558e1329a36d5f9ceab65c52c34e6999822a30164d5c`、实际父script `08758e3f7513367d2fc10b08c0ba6becf0d49028a59ddfd242d75d0f43aadbb9`、父finalreview `e2a7e04555b37b12d5be0c205aca37d3c5eddf7bca7e962848f471574848087f`。官方07源快照fd5614…、归档18eaa7…、根属性ac7fd8…仍用原完整常量核验。

## 确定保留的守卫

- `checked/read`（64–112行）使用Windows原生文件读取与SHA，检查到磁盘根的全部祖先，拒绝符号链接、junction及所有Windows重解析点；普通文件必须单硬链接，并核打开前后文件身份、长度和修改时间。源码名称及补丁路径拒绝越界、大小写别名、特殊Windows路径；文本补丁禁止改名、复制、二进制、链接/子模块及模式变更。
- `validate_contract`（308行起）要求最终格式 `sn100-integration-08-final-merge-revision-02-v1`、本轮源码审查批准且未正式写本修订/未编译/未运行产品。原五行与检查点0至5逐对象等于固定父审查，检查点0至6均覆盖完整联合674表；官方基线663、最终674，最后一片只允许build.sh文本修改，无新增/删除。父完成记录必须真正complete、无在途片、实际五片与预演五片齐全、674终态和脚本/审查/配置全等。
- 正式旧cp5只用于本次写入前校验；正式第6片之后核新cp6。`check_inputs`（375行起）反复重读最终审查、实际新脚本、原始归档/快照、父记录、所有补丁和检查点、旧新直接固定引用及历史原物证。只读这些直接引用，不递归展开历史属性/输入清单内的旧CPP，更不会拿旧正式CPP的SHA当修订后不可变条件。
- 完整源核对（134行起）对正式工程只核选定674表；不枚举、复制或纳入表外229份已有原件。两个已审隔离树及新回演树严格核各自674文件集合、SHA和全文件逐字节。第6片的隔离/正式应用前后均有完整674核对，完整固定输入及属性核对保留。
- 文件操作在Windows原生Python完成。Git始终经系统绝对路径 `wsl.exe --distribution Ubuntu-24.04 --user qyq --cd <实际WSL目录> --exec /usr/bin/env -i … /usr/bin/git`，固定原 `core.autocrlf=false/core.safecrlf=false/core.whitespace=cr-at-eol` 参数，并核各Git根和 `core.eol` 未设置。补丁check/apply使用同一份已核原始stdin bytes，无Shell重解释、忽略空白或Windows默认Git替代。每条Git命令先写命令/目录/stdin指纹，再保留真实stdout、stderr和退出码；非预期退出立即失败。
- 已审 `candidate` 没有独立.git，不能直接当Git根使用。最终源表逐字节以 `stage_directory=candidate` 对照；完整 `--all`、NUL路径输入的属性作用比较则使用正式根、`attribute_stage_directory=ordinary_replay`、本次新回演根。两份既有副本必须先完整674逐字节相等，根属性镜像均为原ac7字节，不修改既有副本。
- 首次 `fix_promotion_progress.json` 及真实命令目录均拒绝已存在，含链接情形。正式第6片之前先写 `current_patch` 和“可能已开始正式写入”并刷新文件及目录。失败记录 `failed_requires_review`，保留补丁、隔离树、日志和在途事实，不恢复、回滚、删除现场或自动重试。保存采用原生文件fsync、同目录临时文件与replace，再由少量固定WSL调用fsync文件/目录；不跳过Windows目录落盘。

## 真实来源和两次执行怎样区分

单修点实际来源采用 `preparation_result.json` 的真实格式 `sn100-integration-08-inventory-fix-v1`：`steps`六步真实0、`ordinary_check_apply_actual_exits=[0,0]`、完整674回演逐字节相等、前后源表及补丁引用闭合、只改build.sh、七源四头各一次、没有构建/输出目录。工具按这些实际字段核验，并读回六步原stdout/stderr和真实退出文件；不使用虚构的 `actual_git_steps` 字段。

旧 `parent_isolated_application` 只核旧五片历史；新 `fix_isolated_application` 只核单修点来源。本次运行还会另做一次独立第6片回演，不把旧“五片通过”字段解释成新“六片重跑通过”。

新progress格式为 `sn100-integration-08-promotion-revision-02-v1`。为了接后续公共工具，`patches/applied/preflight_applied` 是跨两次执行的累计六片；另存 `inherited_applied` 五条原补丁记录、`inherited_preflight=[1,2,3,4,5]`、带真实SHA的 `parent_record/parent_script/parent_review`，以及 `applied_in_this_execution` 的第6条记录、`preflight_in_this_execution=[6]`。`scope` 明说本次只补一片。完整base/final表、官方07快照、实际新script SHA、最终review SHA、实际Git参数、属性和合计changed/added/deleted均供公共 `promotion()` 等值核验。新的工具配置必须采用新格式，不能改旧冻结配置去迁就。

本轮 `compiled=false` 只表示这个修订尚未编译；记录明说旧构建主动停止的实际退出13，历史失败原件也由最终review固定。不写旧批次“从未尝试编译”的虚假结论。

## 已实际执行的工具检查

解释器固定为 `C:/Users/52400/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe -X utf8`，未用WindowsApps占位或改PATH。

| 检查 | 真实结果及固定证据 |
| --- | --- |
| 只解析脚本文本为Python语法树，不import/执行主流程 | 退出0；`tool_ast_result.json` SHA `d84d9e7432d403618493d24309de96a312732bc1a627c733eeee0a193c9eac50`，其中脚本SHA为本报告89edb4…，两处应用调用分别为隔离目录和正式ROOT |
| `--help` | 退出0；`promoter_help.actual.exit` 为原字节 `0\n`，stdout SHA `5953deb886dc1f0e66be092717995dc4100037b2d8bef60ab8879d22f06707af`，stderr为空；未进入主流程 |
| 专用普通小文件落盘 | `check_durability_helper.py` 从上述脚本AST原样提取helper，原生先fsync专用 `durability_probe_01/probe.txt`，再以相同固定WSL调用执行O_RDONLY文件fsync及目录fsync；真实退出0，前后SHA均 `628c0ce809683c7bc7f72703b8d815d02d3379e85882313095e90f37417195eb`，内容相同 |

落盘检查结果 `durability_probe_01/result.json` SHA `ce42b8efe3f31d71e9f7367251bd3a3475ebe709d1dbb03e64ef4d40abab0e91`；命令原件SHA `62af3c4a544a03ff465686b9bf9570b297f38cb3ece8a10e0d4c3cdb351ee7c4`。原stdout为空，132字节stderr是本机WSL启动器代理提示，SHA `b280c454d6f559e56eb3f5ed39f3ba084b8d1744a3ff7fe016b21ff86dea1aca`，原样保留，不凭提示伪造失败或清空日志。工具记录仍以真实退出码为准。

这些操作只涉及本新目录的工具、小文件和帮助输出；正式源码、旧08冻结输入、数据库、产品、网络、构建、既有进程均未由本次工具检查操作。实际 `fix_promotion_progress.json`、实际 `fix_promotion_commands/` 和最终review在报告冻结前均不存在。没有做晋级性能基准、断电恢复、Windows业务运行或电脑脱机测试。

## 最后绑定及尚未验证

根的组装脚本须保持第6行 `after_checkpoint` 和 `checkpoints[6]` 都含 `index=6`，逐对象等值；该处原草稿缺index的问题已通知根，根已确认修正。实际工具/报告先固定SHA，再冻结最终review；工具不内嵌未知最终reviewSHA，执行时强制传 `--final-merge-review-sha256`，并在后续每个关键阶段重读该原件和自身SHA。

本报告未执行 `validate_contract`、全表扫描、Git检查/应用或晋级主流程；最终review也尚未绑定，因此只判断工具合同与已给实际来源字段相配，不能宣称实际晋级成功。小文件检查只验证当前文件系统上的文件/目录fsync基础操作，不能替代突然断电或完整故障恢复验证。正式第6片、完整ARM64构建、板B/RK、真实仪器及资源影响仍由根按实际结果分别登记。
