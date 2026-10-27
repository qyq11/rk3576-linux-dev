QT += core gui widgets sql testlib printsupport
CONFIG += c++14 console testcase warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = instrument_page_test
INCLUDEPATH += $$PWD/..
SOURCES += instrument_page_test.cpp
include(../shell/shell.pri)
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
RCC_DIR = .rcc
