# 板A直接进入root的有限恢复输入

2026-11-02，用户要求板A屏幕接键盘和WindTerm都直接进入root；同时将原自动登录lckfb的三针调试控制台改为root。一次重启及实际会话身份核对通过。[实际验证范围](../../../04_references/workspace_setup/root_session_board_a_20261102/README.md)。

| 文件 | 用途 |
| --- | --- |
| `before.json` | 记录板A身份、本轮新增文件原先不存在、新会话UUID及原会话数量；不含认证数据。 |
| `tty1_autologin.conf` | 从板A读回的屏幕root自动登录设置；安装位置见`before.json`。 |
| `ttyFIQ0_autologin.conf` | 从板A读回的三针调试口root自动登录设置，保留1500000速率。 |
| `windterm_session_added.json` | 本轮新增的“板A-root”会话；只有程序路径、连接参数和标识，没有私钥内容或密码。 |
| `SHA256SUMS` | 在本目录WSL终端执行`sha256sum -c SHA256SUMS`核对文件。 |

原登录模板及串口`autologin.conf`未修改，所以不额外复制整个系统环境；WindTerm原5个会话未修改，所以不备份整份含认证数据的配置。root授权文件原先不存在，本轮新增后用于持续登录；不把该授权文件或电脑私钥放入项目。

同日用户将会话显示名称改为“lckfb-板A”，连接参数和UUID未变；本目录JSON仍保留首次创建时的“板A-root”，恢复时按UUID定位。

## 需要恢复原登录行为时

先通过序列号`3bf8222fe1417f6a`或已核实的SSH指纹确认是板A，再核查后续是否有其他登录设置。停用本轮屏幕和调试口自动root，只需移除两份本轮新增的`zz-board-a-root-autologin.conf`，保留原串口`autologin.conf`，执行`systemctl daemon-reload`、`sync`并重启。屏幕恢复原登录提示，三针口恢复原lckfb自动登录。

WindTerm正常退出后，从`user.sessions`删除与`windterm_session_added.json`中UUID完全相同的新增会话，保留所有其他会话，再重新打开WindTerm。

如同时撤回本轮新增的远程root授权，先保证还能用原lckfb账户登录，再核对`/root/.ssh/authorized_keys`是否只有本轮电脑公钥。仅在确认没有其他或后续授权后删除该新增文件；保留原本存在的`/root/.ssh`目录，不删除电脑密钥。不要根据本文删除其他板或其他人的授权。

本次没有执行回退；文件与来源核对通过不代表恢复后的登录已验收。回退后需要重新核对本地屏幕、调试口和远程登录。
