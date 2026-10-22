# WindTerm lckfb 会话自动进入 root 的回退资料

2026-10-22。来源为本机 `C:/WindTerm/.wind/profiles/default.v10/terminal/user.sessions` 中现有 `lckfb` 会话，UUID 为 `e6362478-eaa7-415c-a20b-7a8e610b2f31`。

只添加两项：`session.autoExecution` 为 `sudo -n -i \r`，`session.autoExecutionDelay` 为 1 秒。SSH 继续使用既有 lckfb 认证，连接成功后自动进入 root 登录 shell。root 的主目录是 `/root`，lckfb 的主目录仍是 `/home/rkshare/lckfb`；未修改板端 root SSH 登录策略、密码、密钥或 sudo 授权。

- `before.json` 仅保存这两个字段的原始存在状态和值，不含认证信息。
- `after.json` 保存本次设置及结构核对结果。
- `restore.ps1` 在 WindTerm 已退出时运行，仅恢复目标会话的这两个字段；保留当前其他设置和认证数据。
- `SHA256SUMS` 用于核对本目录文件。

未复制整个 user.sessions，因为其中包含应用保存的认证数据。原始操作与验证日志在项目 `temp/root_session/`，本目录不归档认证凭据。

已核对保存后的配置、原会话 UUID、会话数 5，以及其余设置和认证字段保持不变；通过分配 PTY 的独立 SSH 执行相同命令，实际返回 `AUTO_ROOT_USER=root`、`AUTO_ROOT_UID=0`、`AUTO_ROOT_HOME=/root` 和 `AUTO_ROOT_CHECK=PASS`。本次没有启动 WindTerm 采集会话界面，首次 GUI 连接仍以实际打开该会话为准。完整过程见[WindTerm 记录第 8 节](../../../02_records/20261021_workspace_setup.md#8-rk-会话连接后自动进入-root2026-10-22)。
