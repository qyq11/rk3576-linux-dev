QT += core gui widgets sql testlib
QT -= network
CONFIG += c++14 console testcase warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = history_page_guard_test
INCLUDEPATH += $$PWD/..
SOURCES += $$PWD/history_page_guard_test.cpp
include(../history/history_core.pri)
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
