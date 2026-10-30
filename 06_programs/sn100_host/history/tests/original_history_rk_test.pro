# 原表匿名夹具，ARM64交叉编译后仅在RK执行，不构建或运行电脑产品测试。
QT += core gui widgets sql printsupport xml qml network charts testlib
CONFIG += c++14 console testcase warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = original_history_rk_test
INCLUDEPATH += ../..
SOURCES += original_history_rk_test.cpp
# 正式历史页与报告引擎的依赖保持和主程序一致，不另拼旧版存储对象。
include(../../shell/shell.pri)
MOC_DIR = .moc
OBJECTS_DIR = .obj
