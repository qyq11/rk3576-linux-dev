# RK按钮测试使用的QtTest库

本目录保存与现有Qt 5.15.13匹配的ARM64测试库安装包，供交叉编译和恢复独立测试依赖。包来自Ubuntu官方ports站点，包名为`libqt5test5t64`，版本为`5.15.13+dfsg-1ubuntu1`，架构为`arm64`。下载日期、来源地址、包和库的SHA-256见[source.json](source.json)，包校验见[SHA256SUMS](SHA256SUMS)。

此次只把包内`usr/lib/aarch64-linux-gnu/libQt5Test.so.5.15.13`及版本链接补入项目的`temp/sn100_host/rk_prepare/sysroot/`。没有向WSL或RK系统安装包，没有修改APT软件源；没有覆盖现有QtCore、QtNetwork及其他库。测试程序在电脑只交叉编译，功能由主窗口上传RK核对。

恢复时先核对包的SHA-256和`dpkg-deb -f`显示的包名、版本、架构，再将包解到临时目录；只复制上述QtTest库及版本链接到同版本交叉编译sysroot。不要把整包解到系统根目录。`readelf -h`必须显示AArch64。正式应用不依赖QtTest；板端独立测试若缺该库，只在测试私有库目录使用，不替换系统Qt。

最初尝试官方`pool/main`地址返回404，随后在同站`pool/universe/q/qtbase-opensource-src/`目录找到精确同版本文件；没有改用其他版本。
