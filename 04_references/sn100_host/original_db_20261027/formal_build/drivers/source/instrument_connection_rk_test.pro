QT += core gui widgets sql xml printsupport qml network charts svg testlib
CONFIG += c++14 console testcase warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = instrument_connection_rk_test
INCLUDEPATH += $$PWD/..
SOURCES += instrument_connection_rk_test.cpp
include(../shell/shell.pri)
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
RCC_DIR = .rcc
