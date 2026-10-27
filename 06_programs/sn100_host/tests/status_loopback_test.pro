QT += core network testlib
QT -= gui
CONFIG += console testcase c++14 warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = sn100_status_loopback_test
include(../protocol/status_loopback.pri)
SOURCES += status_loopback_test.cpp ../protocol/frame_codec.cpp ../protocol/status_session.cpp
HEADERS += ../protocol/frame_codec.h ../protocol/status_session.h
MOC_DIR = .moc
OBJECTS_DIR = .obj
