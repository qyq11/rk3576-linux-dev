# 撤回板B的WindTerm新增会话

2026-11-02，本次只在电脑新增`LCKFB-B`，没有修改板端。原有6个会话保留。

| 文件 | 用途 |
| --- | --- |
| `added_session.json` | 本次新增的会话，包含程序、连接参数和唯一编号。 |
| `known_hosts_rk3576_board_b` | 板B公开的SSH主机身份，用于确认连接对象。 |
| `before_state.json` | 记录新增前同名会话及独立身份文件是否存在。 |
| `SHA256SUMS` | 文件指纹，核对本目录的恢复输入是否完整。 |

需要撤回时先正常退出WindTerm。在`C:\WindTerm\.wind\profiles\default.v10\terminal\user.sessions`中仅移除唯一编号为`105421f6-cf23-42fe-b8b2-51f3c85917ba`的会话对象，保持其余会话不变。`before_state.json`记录独立身份文件原先不存在；确认没有其他连接使用后，可移除本次新增的`C:\Users\52400\.ssh\known_hosts_rk3576_board_b`。不要用整份旧会话文件覆盖后来保存的其他设置。

核对撤回结果：`LCKFB-B`消失，其他会话仍在。重新添加时可用`added_session.json`，并将公开身份文件放回上述位置；本电脑原有私钥仍需可用。本目录不包含密码、私钥和已有会话的认证数据。[本次连接结果](../../../04_references/workspace_setup/windterm_board_b_20261102/README.md)。
