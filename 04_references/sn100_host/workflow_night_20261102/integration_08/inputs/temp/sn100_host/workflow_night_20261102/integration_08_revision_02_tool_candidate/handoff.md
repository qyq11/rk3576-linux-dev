# 第08批修订02工具候选

本目录是与原工具目录同级的新候选。仅复制八份工具并改两个工作目录点，没有改原配置77a、旧工具、原晋级记录95b/最终review e2a或旧构建现场。新配置 frozen=false，未知实际原件记录为空；没有执行构建、联网、板端保存、归档或阶段预检。

## 两处代码变化

- `integration_08_tool_support.py:14`：WORK 改为 `TASK / "integration_08_revision_02"`。
- `build_integration_08.sh:10`：work 改为 `$task/integration_08_revision_02`。

独立逐字节核对确认，这两份文件只有上述各一行替换，其余六份源码完全相等。TOOLS/TASK/ROOT 的同级目录推导不变，CONFIG自动指向本目录。ARCHIVE/EVIDENCE仍指原正式 `integration_08`，批次、格式和远端上传前缀仍为08；它们不应跟着改成修订工作目录名。

新工作目录将隔离日志、对象、程序包和阶段凭据，不借用旧WORK的任何部分产物。实际新工作目录本次没有创建；每个阶段仍要求真实退出和完整终态，拒绝已有输出、不自动恢复或重跑。

## 后续待根固定

根给出的格式名称已作为数据填入：最终元数据为 `sn100-integration-08-final-merge-revision-02-v1`，实际晋级记录为 `sn100-integration-08-promotion-revision-02-v1`。最终review、完整源码manifest、基线包/快照、实际promoter/progress、完整current和evidence/tools记录仍待实际原件，不猜SHA或对象数量。

本次整体六片是跨两次的累计结果：父记录中的1–5已执行，本次只追加第6。最终元数据必须明确父95b/e2a/08758身份、继承的1–5和本次[6]；不能把合计六片写成又执行或重演了六片。工具继续核完整base/final文件表及动态patches/applied/preflight等终态，不增加固定“六片”常量。

根实际提供新记录后，应生成本目录的新冻结配置，更新全部八份工具的路径和实际SHA；不能直接复制旧77a配置并保持frozen=true。已完成的独立差异核对不替代新元数据与实际终态的文件预检。

## 本次实际检查与中断边界

六份Python只做AST解析，实际退出0；两份shell只做 `bash -n`，各退出0，详情见validation.json。独立审者也只读核对了新旧字节差异、同级路径与旧工具/配置指纹，没有执行阶段。

总窗口回报旧构建经核实进程组后停止，真实WSL返回13。本窗口没有将13说成编译失败或通过；只读时旧 `build_exit.txt` 不存在，没有补造退出文件。旧日志、对象和其他现场均保留，由根回收真实中断证据。

正常后续顺序仍是新工作目录的 build → stage → archive → collect → finish；此处只交候选，根尚未下发新最终记录，不启动该顺序。
