# lckfb 主目录登记变更的最小回退资料

2026-10-22，来源为 `lckfb@192.168.100.123` 的实际 SSH 核查；[原始查询日志](../../../04_references/workspace_setup/home_path_20261022/inspect_20261022.log)与[后续核查输出](../../../04_references/workspace_setup/home_path_20261022/)已从临时目录归档。

本次只将账号登记的主目录从 `/home/lckfb` 改为 `/home/rkshare/lckfb`，验证后删除旧软链接。真实目录、共享内容、权限、公钥和 Samba 配置保留。这里只保存恢复本次变更所需的账号条目和原链接目标；不备份整机或复制用户文件，不归档密码、密码哈希、私钥或公钥内容。

- `lckfb.passwd.before`：原账号条目，密码字段为占位符 `x`。
- `lckfb.passwd.after`：变更后经独立 SSH 查询核实的账号条目。
- `old_link.target`：原 `/home/lckfb` 软链接目标。
- `preserved_files.sha256`：共享、SSH、主机名及现有授权公钥文件的变更前 SHA-256，用于核查保留情况，不包含认证内容。
- `restore.sh`：需要回退时在板端以 `sudo bash restore.sh` 执行；只重建兼容链接并恢复该账号的主目录字段，不覆盖整个 `/etc/passwd`。
- `set_home.sh`：与恢复脚本一同放在板端；通过 `vipw` 的文件锁只改 `lckfb` 一行的第六字段，保留现有登录会话。仅接受本次变更涉及的两个路径。
- `SHA256SUMS`：本目录文件校验清单。

恢复脚本只用于主动回退，不是日常启动步骤。恢复后的独立 SSH 登录仍需实际核查；本次不为测试回退而撤销用户要求的新布局。

变更与验证已完成：旧链接不存在；独立免密 SSH、`HOME`、登录位置、`cd`/`~`、普通用户主目录与共享根目录文件读写全部通过；上述配置和公钥文件 SHA-256 不变。已有用户服务管理器的 `HOME` 同步改为新路径；如主动回退且需立即更新该运行时环境，应在该用户会话中执行 `systemctl --user set-environment HOME=/home/lckfb`。已打开的 shell 在重新连接后更新环境。本次没有重启或执行恢复脚本，未把 SSH 读写当成 Windows SMB 验证。完整过程见[共享记录第 7 节](../../../02_records/20261021_Samba共享文件夹.md#7-账号主目录统一到共享目录2026-10-22已完成)。
