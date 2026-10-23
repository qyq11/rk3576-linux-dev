QT += core gui widgets sql testlib printsupport
QT -= network
CONFIG += c++14 console testcase warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = integrated_report_test
INCLUDEPATH += $$PWD/.. $$PWD/../history
SOURCES += integrated_report_test.cpp
include(../shell/shell.pri)
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
RCC_DIR = .rcc
