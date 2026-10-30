QT += core gui widgets sql xml printsupport qml network charts svg testlib
CONFIG += c++14 console testcase warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = original_main_task_chain_rk_test
INCLUDEPATH += $$PWD/../..
SOURCES += $$PWD/original_main_task_chain_rk_test.cpp
HEADERS += $$PWD/original_task_flow_fixture.h
include(../../shell/shell.pri)
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
RCC_DIR = .rcc
