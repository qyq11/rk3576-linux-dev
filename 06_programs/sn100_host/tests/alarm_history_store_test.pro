QT += core sql testlib
QT -= gui
CONFIG += console c++14 testcase
CONFIG -= app_bundle
TEMPLATE = app
TARGET = alarm_history_store_test
INCLUDEPATH += $$PWD/../alarm_history
include($$PWD/../alarm_history/alarm_history_core.pri)
SOURCES += $$PWD/alarm_history_store_test.cpp
MOC_DIR = .moc
OBJECTS_DIR = .obj
