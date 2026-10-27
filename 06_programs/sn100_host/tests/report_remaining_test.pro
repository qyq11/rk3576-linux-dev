QT += core gui widgets sql testlib
QT -= network
CONFIG += c++14 console testcase warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = report_remaining_test
INCLUDEPATH += $$PWD/.. $$PWD/../history
SOURCES += report_remaining_test.cpp
include(../history/history_core.pri)
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
