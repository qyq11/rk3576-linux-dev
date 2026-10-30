QT += core network testlib
QT -= gui
CONFIG += c++14 console testcase
TEMPLATE = app
TARGET = device_control_rk_test
INCLUDEPATH += $$PWD/..
SOURCES += device_control_rk_test.cpp
include(../instrument/device_control.pri)
MOC_DIR = .moc
OBJECTS_DIR = .obj
