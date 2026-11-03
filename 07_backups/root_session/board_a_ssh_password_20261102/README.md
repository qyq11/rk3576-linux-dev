# 板A远程密码登录的有限恢复输入

2026-11-02，板A序列号`3bf8222fe1417f6a`，SSH主机指纹见`before_state.json`。用户选择让各台电脑使用同一root系统密码远程登录；本次只新增`/etc/ssh/sshd_config.d/20-board-a-root-password.conf`并重新加载SSH服务。

修改前该文件不存在，原SSH主配置不变，因此无需复制整套SSH配置或任何认证文件。`before_state.json`记录原状态；`20-board-a-root-password.conf`是实际生效文件；`restore.sh`用于撤回本次开关；`SHA256SUMS`核对这3份文件。这里不含密码、密码摘要、授权文件、主机密钥或用户文件。

需要恢复时，先核实板A身份，把本目录整体复制到板A独立目录，root执行`sha256sum -c SHA256SUMS`，通过后执行`bash restore.sh`。脚本核对当前主配置及新增文件未被后续修改，再移除新增文件、检查配置并重新加载SSH；失败时恢复本次开关。恢复后root继续允许密钥登录，密码远程登录重新关闭，系统密码保持原值。

本轮只检查恢复脚本语法，未执行恢复，也未重启板卡。实际操作及密码登录结果见[本轮证据](../../../04_references/workspace_setup/ssh_password_board_a_20261102/README.md)，不适用于板B。
