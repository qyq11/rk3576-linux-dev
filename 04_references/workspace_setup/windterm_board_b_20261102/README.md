# 板B的WindTerm连接

2026-11-02，按用户要求新增`LCKFB-B`。使用有线地址`192.168.100.123`、端口22和root账户，连接后目录为`/root`。

打开WindTerm，双击`LCKFB-B`即可连接。此会话与板A现有入口一样，通过Windows自带SSH程序使用本电脑已有密钥登录；它是终端入口，不提供WindTerm原生SSH会话的文件管理面板。系统密码未改变。

## 已核对的范围

- 用会话中保存的同一程序和参数实际连接，硬件序列号为`8314b1e8ffd0321f`，root账户、用户编号0及`/root`均正确，退出0。[实际输出](saved_arguments_login.log)
- 使用板B独立身份文件`C:\Users\52400\.ssh\known_hosts_rk3576_board_b`，SSH主机指纹为`SHA256:S4ihaw0V/Z8w7v3VlJOQEFLEH3Hp6nr3KjiUTxanwPg`。连接对象不符合板B身份时会拒绝连接。
- 正常退出WindTerm后写入，原6个会话逐项比较保持一致，新增后共7个。[核对结果](result.json)
- 电脑单独连接验证通过；WindTerm界面双击连接尚未代验。本次只调整电脑会话，没有新增板端业务或仪器通过结论。

本次无线旧地址`192.168.3.59`连接超时，因此正式入口使用有线地址。临时检查和配置脚本在`temp/windterm/board_b/20261102/`；[恢复所需资料](../../../07_backups/root_session/board_b_windterm_20261102/README.md)仅含新增会话和公开主机身份，不含密码、私钥或整份会话配置。

完整过程见[WindTerm统一记录](../../../02_records/20261021_workspace_setup.md#board-b-windterm-20261102)。
