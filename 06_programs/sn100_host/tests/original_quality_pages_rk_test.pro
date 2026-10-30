QT += core gui widgets sql testlib network xml printsupport charts qml svg
CONFIG += testcase c++14
TEMPLATE = app
TARGET = original_quality_pages_rk_test
include(../shell/shell.pri)
SOURCES -= $$PWD/../shell/main.cpp
SOURCES += $$PWD/original_quality_pages_rk_test.cpp
