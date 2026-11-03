# 板B开机与远程登录的有限恢复输入

2026-11-02，板B序列号`8314b1e8ffd0321f`。用户要求与板A已记录方式一致，开机输入root和本轮指定的小写密码，进入`/root`；同时允许root密码远程登录。只新增两个本地终端补充配置和一份SSH补充配置，未覆盖原服务模板、原三针口配置、SSH主配置或密钥。

`before_state.json`登记身份和三个新增文件原先不存在；两份`*_password_login.after.conf`及`20-board-b-root-password.conf`是实际生效文件。`SHA256SUMS`用于核对本目录文件。这里没有密码、密码摘要、认证数据库或本机加密凭据。

如需撤回本次登录开关，先核对板B身份和后续修改，复制本目录到板B，执行`sha256sum -c SHA256SUMS`，通过后由root执行`bash restore.sh`。脚本只移除本次三个文件并重载服务；本地终端还需重启后再核。原三针口配置会重新自动登录lckfb，root远程密码登录会关闭，原密钥仍保留。

此恢复不恢复以前的root密码，也不改Samba或WiFi；共享单独用[板B共享恢复输入](../../samba/board_b_root_20261102/README.md)。无线连接凭据不归档；撤回新增WiFi时在有线连接下用`nmcli connection delete uuid 2e1ef016-7e35-4008-95a0-bc6f4cd0f2af`，不要删除原有有线配置。回退未执行，实际结果见[板B记录](../../../04_references/workspace_setup/root_login_board_b_20261102/README.md)。
