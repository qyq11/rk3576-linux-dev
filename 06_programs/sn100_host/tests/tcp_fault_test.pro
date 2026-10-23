QT += core network testlib
QT -= gui
CONFIG += console testcase c++14 warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = sn100_tcp_fault_test
SOURCES += tcp_fault_test.cpp \
           ../protocol/tcp_transport.cpp \
           ../protocol/read_only_messages.cpp \
           ../protocol/frame_codec.cpp \
           ../protocol/status_session.cpp
HEADERS += ../protocol/tcp_transport.h \
           ../protocol/read_only_messages.h \
           ../protocol/frame_codec.h \
           ../protocol/status_session.h
INCLUDEPATH += ../protocol
win32:LIBS += -lws2_32
MOC_DIR = .moc
OBJECTS_DIR = .obj
RCC_DIR = .rcc
