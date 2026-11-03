# 第七批源码晋级工具独立窄复核

结论：所核固定生成器与模板未发现确定阻断，可由根按交接先生成脚本、核实际SHA，再单独决定执行晋级。本复核没有生成promoter、应用补丁或写正式源码，也没有运行产品、构建、操作数据库、网络或板卡。

只新增本报告，固定源码、阶段、最终metadata和旧cross报告均未改。检查方式为源码阅读、固定引用/实际文件指纹核对，以及一次仅在内存读取文本、AST解析和替换固定SHA的分析；未import、exec或调用生成器/模板/晋级脚本的业务代码。

## 固定身份

| 输入/内存结果 | 实际SHA-256 |
| --- | --- |
| `prepare_integration_07_promotion.py` | `a667a2e5b6ed90d683ecdac412e19448da0680fab14bf24714e8b2252107d130` |
| `promote_integration_07_template.py.in` | `9789ba40cf805a137aa92197224d0d0f3d328ded4a21480e8a7b1dbf45fca750` |
| `integration_07_promotion_tools_handoff.md` | `c9f915fd1c9b54d7d14791f928b19a9a28de25a6f091e02d9432fe9d76fb0574` |
| 根显式固定的`final_merge_review_with_shutdown.json` | `2b4730874ad721db6b66ed44195e2f202adac4e95ee697b23f5062c0b2599526` |
| 只在内存将唯一marker替换为上述根SHA后的脚本文字 | `3b2a7f13c22a0cdcec4c3b9d497d3dbeee4ee2afa83bc9fe464e93d1df65a909` |

生成器、模板及替换后的文字均通过AST解析。本复核没有将内存结果写成`.py`；核对时`promote_integration_07_review.py`仍不存在，不能将预期指纹写成已生成或已执行的脚本。

固定metadata的39项引用（含重复的阶段前后引用）实际SHA全部相符。五个完整检查点连续，均覆盖官方649项与新增14项构成的663路径；最后一份等于最终源表，四片顺序为N03、producer适配并集、真实READY日志、外层窗口关闭重试。

当前正式649项实际SHA仍符合官方06，14个新增目标未存在；最终stage和独立replay的663项实际SHA仍符合固定760d81b3阶段源表。这里只核当前事实及将来的检查逻辑，没有运行preflight或正式晋级。

## 显式生成与来源约束

- 生成器必须收到`--final-merge-review-sha256`，检查64位格式并与当前metadata原字节SHA相同，没有默认选择“最新”清单。模板也只认生成时固化的同一SHA。
- 官方快照48c4010b、源码包c1a21586、根`.gitattributes`5de2ac8a均固定；base/final/replay源表、4片/5检查点、39变化含14新增且无删除都在生成前校验。补丁、cross报告、历史失败记录和属性镜像引用也按实际SHA核对。
- 模板只替换一个marker，模板本身受9789ba40固定；生成前后再次核metadata/模板未变，输出用`xb`拒绝覆盖，flush/fsync文件并在POSIX同步目录。这里只输出临时晋级脚本，不调用Git或构建。
- 模板运行门只允许Linux/Ubuntu-24.04、非root的qyq及`/usr/bin/git`；使用实际回演的autocrlf=false、safecrlf=false、whitespace=cr-at-eol，并要求core.eol未设置。没有按Windows Git默认行尾代替。

## 路径与未涉足文件

源表及补丁目标限定`06_programs/sn100_host/`。路径检查拒绝绝对路径、上级目录、空段、Windows分隔/盘符、控制字符、链接/目录连接及解析后出界；读取要求普通文件。补丁解析只接受已审文本窄片，拒绝二进制、改名/复制、模式变更、链接/子模块、重复或不一致目标；目标还必须属于663路径集。

正式初始检查同时核649既有文件和14个新路径不存在，新增路径已有文件、目录或链接时拒绝，不覆盖。修改目标另核单链接。用户其他未提交文件不被枚举删除或覆盖：若属于649底稿且内容不同则停止，若不在补丁和源表中则保留。这里不执行Git reset/clean、回滚或整目录替换。

## 预演、属性和逐片进度

模板在任何正式写入前先核全部固定引用、补丁SHA、全初始源和stage源。新建随机名预演目录，复制已核原源并逐字镜像根`.gitattributes`，建立独立Git上下文；比较正式/预演/stage的实际check-attr结果。四片都使用同一WSL Git普通`apply --check`再`apply`，每次前后核完整检查点；最终663路径还与冻结stage逐字节比较。没有ignore-space-change、reject、自动三方应用或空白自动修复。

正式开始前再次核全原像和全部固定输入；每片写入前复核当前完整检查点、所有输入和自身脚本未变，先fsync落盘`current_patch`及`formal_write_may_have_started=true`，再执行该片。应用完成且整表核对后才登记applied并清current_patch。因此检查或应用失败时，进度仍保留“哪片可能已经写入”，不会只登记成功片而丢掉当前风险范围。

首次进度用排他创建，已有任何07进度就拒绝运行。后续进度以临时文件flush/fsync后原子替换并同步父目录；异常保留现场，写failed_requires_review、complete=false后重新抛出，真实退出非0。没有自动resume、rollback、删除旧进度或续用06特殊恢复逻辑。

只有四片全部完成、正式663路径与冻结stage逐字节相同、固定输入和自身脚本仍一致，才记录complete=true。完成时changed/added/deleted重新按全表计算并排序，自身实际SHA从第一份进度起就保存。进度不是多文件事务保证；如中途失败，需根依据当前正式内容和在途片人工审查，不能重跑绕过。

## 交接限制

本次结果只支持根继续做“生成→核新脚本实际SHA/内容→单独执行”的既定步骤，不表示已晋级。实际生成结果应等于内存预期3b2a7f13…，不相等就先查明；该核对不替根执行，也不自动触发下一步。

业务跨片结论仍引用原固定`integration_07_review/cross_patch_review.md`（fde8a4b0…），未扩大重审。后续完整ARM64构建、板端保存、运行、真实N在途关闭和资源结果需分别有实际证据；工具中的源码一致和READY日志不代表这些验证通过。
