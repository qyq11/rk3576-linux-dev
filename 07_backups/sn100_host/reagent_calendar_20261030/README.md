# 试剂日历与列表顺序：匹配恢复输入

本目录对应正式发布`ef294a921e7eec2d293beb5aa0fbe5784d6b5056cef92648e799c3529b839a14`。用途是在应用更新失败时恢复本轮程序，或按更新前指向回到a247d2f8；不包含也不恢复现场患者、账户、业务数据库或系统环境。

- `sn100_shell_ef294a921e7eec2d293beb5aa0fbe5784d6b5056cef92648e799c3529b839a14.tar.gz`：本轮主程序、报告辅助程序及必要库/资源。包SHA-256为`d37170cced425523b45abbeddc24cafe577bd55bf83f14c06ee192845e73bd3e`。
- `sn100_calendar_sources.tar.gz`：832份匹配工程文件；`source_bundle_identity.json`逐份绑定源码指纹。测试控件名修订及指纹在`test_input_revision.json`；编译后仅清单/测试变化的依据在对应证据目录。
- `source_snapshot.json`：本轮607项正式打包输入。
- `application_before.txt`：更新前current=a247d2f8、previous=f8c的指向及启动封装指纹，没有认证内容。旧a247匹配包仍在[上轮恢复目录](../resume_20261030/README.md)。
- `SHA256SUMS`：先核对本目录文件内容，全部一致后才可恢复。压缩包适用现有Git LFS规则；新电脑先拉取LFS文件。

恢复前先正常关闭程序并取得`/home/rkshare/programs/sn100_shell/lifecycle.lock`，解包到新的应用版本目录，用包内rk_board.sh核完整文件和动态库，再按application_before.txt恢复应用指向。保留runtime原位，不把源码包或程序包覆盖到运行数据根；恢复后还需核启动、正常关闭和重开。仅恢复版本指向不能证明业务数据已经恢复。

本轮只改应用版本和current/previous指向，没有改启动封装、系统Qt/内核或现场配置。原a247程序包此前已经归档并重新核对，不为本次重复备份整机或运行库。[实际结果与边界](../../../04_references/sn100_host/reagent_calendar_20261030/README.md)。
