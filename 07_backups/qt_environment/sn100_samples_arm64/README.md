# SN100 样本界面 QtSvg ARM64 恢复输入

原版样本下拉框使用 SVG 箭头。本归档补充与现有 Ubuntu noble Qt 5.15.13 一致的 QtSvg 运行库、插件与开发头文件，不改变箭头绘制与界面布局。

来源为 Ubuntu 官方 ports 仓库的 **noble / universe / arm64**，固定 `libqt5svg5` 与 `libqt5svg5-dev` 的 `5.15.13-1`，未使用更新版本替代。包依赖声明 `qtbase-abi-5-15-13`。两个 `.deb` 已由仓库现有 `07_backups/**/*.deb` Git LFS 规则覆盖。

- `SHA256SUMS`：两个包的官方索引 SHA-256。
- `package_index.txt`：官方 `Packages.xz` 中两个包的完整条目。
- `InRelease`：同次下载的 noble 签名发布索引；`index_signature_status.txt` / `index_signature_verify.log` 保存使用 WSL 已有 Ubuntu archive keyring 验签的结果。
- `source.json`：下载 URL、完整 Packages.xz 校验值、版本与恢复边界。完整压缩索引留在临时验证目录，归档保存相关条目与签名发布索引，复核时可重新从记录 URL 下载并按 InRelease 校验。

在普通 WSL 用户下，由 `06_programs/sn100_host/shell/prepare_runtime.sh ABSOLUTE_PRIVATE_SYSROOT` 连同已有 Qt/SQL/报表归档恢复。该脚本先核对 SHA，要求两个固定包名、版本和 arm64 架构，再仅用 `dpkg-deb -x` 解包至指定私有 sysroot。

本归档恢复范围为 `QtSvg` 头文件、`libQt5Svg.so.5.15.13` 及其别名、Qt5 的 `imageformats/libqsvg.so` 和 `iconengines/libqsvgicon.so`、开发描述文件及 `usr/share/doc/libqt5svg5/copyright`。三个运行文件加入 sysroot 的来源候选清单；正式打包仍由 ELF 闭包检查和明确的插件清单决定，开发头文件不进入运行包。

没有执行系统安装、修改 WSL 系统配置、上传或连接 RK/仪器。本归档不构成板端运行或仪器业务验收。
