# 板A取消自动登录、改为密码登录的恢复输入

2026-11-02，板A序列号`3bf8222fe1417f6a`。用户要求屏幕和三针调试口开机输入密码后进入root，本轮已配置并重启核对；指定root密码的系统登录程序验证通过。[实际范围](../../../04_references/workspace_setup/root_password_login_board_a_20261102/README.md)。

| 文件 | 用途 |
| --- | --- |
| `tty1_autologin.before.conf`、`ttyFIQ0_autologin.before.conf` | 本轮写入前，从板A取回的两份root自动登录补充配置。 |
| `tty1_password_login.after.conf`、`ttyFIQ0_password_login.after.conf` | 重启后从板A取回的两份密码登录补充配置。 |
| `before.json` | 板卡身份、原文件位置、新文件原先不存在及写入范围。 |
| `SHA256SUMS` | 在本目录WSL终端用`sha256sum -c SHA256SUMS`核对以上文件。 |

只备份本轮改动涉及的登录配置；原厂商服务模板和三针口原`autologin.conf`未修改，不额外备份整机。密码、密码摘要、`shadow`文件和本机加密凭据均不在归档中。具体密码保存在本机项目外，本目录不能恢复此前旧密码。

## 如需恢复此前自动进入root的方式

先确认板A身份，并核查没有后续登录设置需要保留。把两份`.before.conf`分别放回原位置`/etc/systemd/system/getty@tty1.service.d/zz-board-a-root-autologin.conf`和`/etc/systemd/system/serial-getty@ttyFIQ0.service.d/zz-board-a-root-autologin.conf`，移除本轮新增的两份`zz-board-a-password-login.conf`，然后执行`systemctl daemon-reload`、`sync`并重启。保留原三针口`autologin.conf`，不要误删其他服务配置。

这种恢复只调整本地登录是否自动进入root，不撤回本轮用户指定的root密码，也不修改SSH密钥或WindTerm连接。恢复后仍需实查本地账户、目录、终端速率和所需功能；本轮没有执行回退，不能把文件校验通过算作恢复验收。
