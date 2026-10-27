QT += core gui widgets sql testlib printsupport
QT -= network
CONFIG += c++14 console testcase warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = report_remaining_html_test
INCLUDEPATH += $$PWD/.. $$PWD/../history
SOURCES += report_remaining_html_test.cpp ../history/history_store.cpp ../history/report_remaining_store.cpp
HEADERS += ../history/history_store.h ../history/report_remaining_html.h
include(../history/history_report.pri)
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
