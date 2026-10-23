QT += core gui widgets sql testlib printsupport
QT -= network
CONFIG += c++14 console testcase warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = history_report_test
INCLUDEPATH += $$PWD/.. $$PWD/../history
SOURCES += history_report_test.cpp
include(../history/history_core.pri)
include(../history/history_report.pri)
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
