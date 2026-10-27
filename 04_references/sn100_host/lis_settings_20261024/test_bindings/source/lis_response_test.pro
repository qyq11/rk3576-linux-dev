QT += core gui widgets printsupport testlib
CONFIG += c++14 console testcase warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = lis_response_test
INCLUDEPATH += $$PWD/..
SOURCES += lis_response_test.cpp
include(../ui/printer_settings.pri)
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
