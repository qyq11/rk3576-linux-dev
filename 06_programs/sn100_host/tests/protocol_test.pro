QT += core testlib
QT -= gui network
CONFIG += console testcase c++14 warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = sn100_protocol_test
SOURCES += protocol_test.cpp \
           ../protocol/frame_codec.cpp \
           ../protocol/status_session.cpp
HEADERS += ../protocol/frame_codec.h \
           ../protocol/status_session.h
INCLUDEPATH += ../protocol
MOC_DIR = .moc
OBJECTS_DIR = .obj
RCC_DIR = .rcc
