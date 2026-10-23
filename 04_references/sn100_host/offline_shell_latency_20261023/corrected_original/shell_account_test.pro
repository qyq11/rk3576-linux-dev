QT += core gui widgets sql testlib
QT -= network
CONFIG += c++14 console testcase warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = shell_account_test
INCLUDEPATH += $$PWD/.. $$PWD/../ui
SOURCES += shell_account_test.cpp
include(../shell/shell.pri)
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
RCC_DIR = .rcc
