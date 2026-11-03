# 板B共享改用root的恢复输入

2026-11-02，板B硬件序列号`8314b1e8ffd0321f`，SSH主机指纹`SHA256:S4ihaw0V/Z8w7v3VlJOQEFLEH3Hp6nr3KjiUTxanwPg`。本目录只适用于板B。

`smb.conf.before`、`smb.conf.after`为实际修改前后配置，仅把`rkshare`共享允许账号从lckfb改成root。路径仍为`/home/rkshare`，原文件和原lckfb共享账号保留；本次新增root共享账号，密码按用户本轮指定的小写值设置。本目录不保存密码、认证数据库或用户文件。

恢复前先核对目标和后续修改，结束该共享的文件传输。把本目录复制到板B，root执行`sha256sum -c SHA256SUMS`，通过后执行`bash restore.sh`。脚本核当前配置指纹，恢复原配置，删除本次新增root共享账号并重启共享服务；此后应使用原lckfb共享登录。它不恢复系统root密码，不删除Windows保存的凭据，不修改原文件所有者、开机登录或WiFi设置。

恢复脚本仅做语法检查，没有执行回退。[本次共享实际结果](../../../04_references/samba/board_b/root_20261102/README.md) · [开机登录恢复输入](../../root_session/board_b_unified_login_20261102/README.md)。
