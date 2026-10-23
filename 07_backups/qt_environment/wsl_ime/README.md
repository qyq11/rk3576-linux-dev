# Ubuntu WSL 拼音输入恢复材料

适用范围：Ubuntu-24.04 / amd64 / qyq 的 WSLg Qt5 图形软件。2026-10-23新增26个包，无已有包升级或移除。包名/版本/来源见`source.json`，安装前后清单和实际结果同目录保存；所有`.deb`按仓库既有规则使用Git LFS。

`packages/`保存Ubuntu官方仓库下载的精确版本安装包，`user_config/`是首次建立的Fcitx5用户配置，`SHA256SUMS`校验本目录恢复输入。配置前`~/.config/fcitx5`无已有profile/config，因而没有覆盖旧配置，也不备份整个Ubuntu或Windows环境。

## 恢复

先在该目录运行`sha256sum -c SHA256SUMS`，核对目标确为匹配的Ubuntu24.04 amd64。包安装需要管理员权限，示例：`sudo dpkg --install packages/*.deb`。这些材料仅补输入法依赖，不负责恢复完整Ubuntu/Qt环境。

以目标普通用户操作：先检查`~/.config/fcitx5/profile`及`config`；已有配置应保留，不能直接覆盖。仅首次配置时建立`~/.config/fcitx5/`并复制`user_config/profile`及`user_config/config`。profile包含美式键盘和拼音，Ctrl+Space切换。

项目`06_programs/sn100_host/wsl_ime.sh`负责在正常WSL GUI启动时连接Qt插件并复用/启动用户Fcitx5。统一入口仍使用`shell/wsl.sh integrated-run`。WSLg实测拒绝Wayland input_method绑定，启动器只在检测到`/mnt/wslg`时为新启动的共享用户daemon禁用该前端，使用X11/DBus与当前XCB程序通信；不会替换既有daemon。不改Windows输入法、系统全局环境、ARM64构建或RK配置。

## 回退边界

启动单次对照可设置`SN100_WSL_IME=off`，跳过Qt输入法适配。完整回退先退出依赖输入法的软件，只停止此次新建且确认无人使用的Fcitx5会话，再处理此次新建的用户配置；保留之后生成的个人配置/词库。包回退须先按`installation_result.json`的新增列表做卸载模拟，确认后续软件未依赖它们再执行，不执行全系统autoremove。已有Qt库、系统设置和医院/账户数据不在回退范围。

实际输入、候选选字、保存与重开验收统一记录在`02_records/20261022_sn100_host_migration.md`的`hospital-ime-setup-20261023`节。
