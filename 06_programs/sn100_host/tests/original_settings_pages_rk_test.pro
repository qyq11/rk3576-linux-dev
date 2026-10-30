QT += core gui widgets sql network testlib
CONFIG += c++14 console testcase
CONFIG -= app_bundle
TARGET = original_settings_pages_rk_test
INCLUDEPATH += ..
SOURCES += original_settings_pages_rk_test.cpp
include(../shell/shell.pri)
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
RCC_DIR = .rcc
