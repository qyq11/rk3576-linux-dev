QT += core gui widgets sql testlib
CONFIG += c++14 console testcase warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = report_dictionary_page_test
# Formal history page and shared DTO; build/runtime are assigned outside the source tree.
INCLUDEPATH += $$PWD/.. $$PWD/../history
include(../history/history_core.pri)
SOURCES += $$PWD/report_dictionary_page_test.cpp
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
