# 板A本次采用SN500主题前的原主题

这是板A实际原文件，SHA见清单。原主题为SN450、普通10孔；本次使用用户已提供的当前SN500主控与5孔主题，只替换这一份主题，连接和其他INI不改。恢复时须先正常关闭本次GUI，再将此原件放回板A`/home/rkshare/programs/sn100_shell/runtime/data/theme_config.ini`并核SHA。替换配置与实际功能验证分别登记；本次未执行恢复。

原`instrument_configuration_file`和`theme_configuration_file`两个指针不存在；本轮新建后分别指板A自己的config.ini和theme_config.ini。恢复指针时只处理这两个本轮新建文件，不能删除既有original_project_root或业务资料。此目录不含账户、密码、患者数据库或板B配置。
