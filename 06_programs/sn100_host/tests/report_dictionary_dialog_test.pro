QT += core gui widgets testlib
CONFIG += c++14 console testcase warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = report_dictionary_dialog_test
include(../history/report_dictionary.pri)
SOURCES += $$PWD/report_dictionary_dialog_test.cpp
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
