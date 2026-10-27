QT += core gui widgets sql testlib
QT -= network
CONFIG += c++14 console testcase warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = qc_local_response_test
INCLUDEPATH += $$PWD/..
SOURCES += qc_local_response_test.cpp
include(../quality_control/qc_standalone.pri)
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
RCC_DIR = .rcc
