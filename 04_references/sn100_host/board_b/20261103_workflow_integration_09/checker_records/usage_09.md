# 第09轮工具调用与待绑定项

本目录是独立候选。现在只有源码和静态核对，尚未冻结正式源包、编译检查器或运行产品。旧08物料和原件不变。总控目前给出的晋级后像为684份，列出输入408；408不是预填的最终对象数量，也不能直接替代正式完成记录中的唯一输入数量。

## 正式构建完成后的一次入口

仅当总控拿到本次ARM完整构建真实退出0、匹配官方源码包及实际发布号后，使用普通WSL用户调用：

```text
python3 /绝对路径/本目录/execute_final_09_build.py --execute \
  --integration-directory /绝对路径/temp/sn100_host/workflow_night_20261102/integration_09_revision_05 \
  --release-id 本次完整发布SHA \
  --source-archive /绝对路径/07_backups/sn100_host/board_b/workflow_integration_20261102/integration_09/sn100_sources_本次发布SHA.tar.gz \
  --source-archive-sha256 本次官方源包实际SHA
```

驱动串行进行官方源冻结、同版检查器编译链接、ARM静态依赖检查，记录真实退出与完整输入前后指纹；不安装环境、不构建正式主程序、不上传或联板。失败保留原件，不覆盖重试或部署旧产物。实际数量从本次清单读取，成功后才有`final_09_handoff.json`及8项`upload_manifest_09.sha256`。

`build_review_09.sh`只另编一个观察CPP；三个观察头都由它包含，不新建工作线程或MOC。链接本次全部主程序对象并保留真实main。正式报告辅助对象单列核对，不拿它们凑主对象数量。对象来源按冻结脚本和输入清单重建，不声称是编译器trace或完整头文件依赖图。

## 板端仍由总控串行操作

构建工具先暂存到`releases/integration_09/<id>`；正常启动器仍只接受平层。总控在锁内核包、复制到新的`releases/<id>`、保存真实旧指针并切换后，才可从独立09 verification目录调用`run_case_on_board.sh auto <release> <appSHA> <checkerSHA> <时长> <独立attempt>`。runner继续严格核`$base/releases/$id`，没有放宽任意路径或直接运行nested暂存版。

启动前由总控在同次SSH核板B公钥/序列、原主控实际进程、3份应用INI和4份主控配置的既定摘要；不能靠启动后的LIS观察才发现端点变了。上次PID只是历史值，不能固定当作当前主控身份。runner仅给本次公开`ini_before.sha256`设`root:lckfb 640`并核lckfb可读，其他umask、配置权限保持。

原24项的Qj状态查询观察段保持；随后原按钮最多明确发送一次Qk当前告警查询，完整TX逐帧审Qj/Qk及BEGIN/END区间。10秒只作本地等待提示，不重发，QK不是Qk确认。原告警页的`btnReturn`只切页；检查器经原`btnClose`等真实关闭和SQL读取收尾，不绕过保护。LIS按原配置自行连接，检查器不发送业务消息、不记录正文或患者信息。

已给定的真实LIS条件为TCP client、127.0.0.1:3800、link=true、autoget/autosend=false、moni=false、linkintervalms=1000、lisCode缺省UTF-8；debug重连原值500毫秒经现有最小值规则实际取2000毫秒。总控当前观察3800无监听，无真实对端则连接成功与LIS业务待验，不建服务器、不回环自测。

## 完成后只读收集与解析

```text
python3 /绝对路径/本目录/collect_case_09.py --execute \
  --remote-case /home/rkshare/programs/sn100_shell/verification/status_login_09_当次日期/runs/auto_独立名称
```

本地默认保存在`temp/sn100_host/workflow_night_20261102/integration_09_live/<同名attempt>`；同级`auto_dispatch.log/.exit`须是本次真实结束记录。默认预期退出0；如果实际失败，明确传`--expected-dispatch-exit`及`--expected-case-exit`的真实数值以保留失败证据，不改成0。工具只读收集已结束case，不重跑、不切版本、不启停程序。

收集仍先全元数据，再白名单取回、内存内容检查、前后元数据核对和后验。数值状态图保留，缓存、业务库、INI正文和非必要登录图不取回。为独立核Qk映射，仅额外只读一份非患者规则`runtime/data/db/systemInfo/abnormal_Config.csv`，最大4MiB；其启动/结束摘要、读取前后SHA、编码及原文/副本SHA另记，不混入case分区文件计数。UTF-8失败时按原GB18030往返规则处理，缺失、变化或脱敏时不冒充原映射通过。

`summarize_run.py`核完整TX、真实退出及09合同，按是否实际打开告警页追加`ALARM_HISTORY_VIEW_CLOSED`条件核对；LIS停止只看正常退出后的真实证据，不在关闭前JSON预填已停。`review_actual_ad_09.py`核原96格及新增实际观察；原始Qk/QK还要独立组帧、按原字段和规则CSV映射核产品观察及当前表格。无实际Qk仍为待验；不能造包或把QK当确认。

所有实际检查数、可选项数和关闭条数都取本次数组/合同。Task/N真实业务、在途一次关闭、管理员密码、实体输入/屏幕、实际测试绿灯及真实仪器验收继续待对应实证。资源只记录实际短窗，不判预算、控制时限或长稳通过。
