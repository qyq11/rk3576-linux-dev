QT += core network testlib
QT -= gui
CONFIG += console testcase c++14 warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = sn100_tcp_transport_test
SOURCES += tcp_transport_test.cpp \
           ../protocol/frame_codec.cpp \
           ../protocol/status_session.cpp \
           ../protocol/read_only_messages.cpp \
           ../protocol/tcp_transport.cpp
HEADERS += ../protocol/frame_codec.h \
           ../protocol/status_session.h \
           ../protocol/read_only_messages.h \
           ../protocol/tcp_transport.h
INCLUDEPATH += ../protocol
MOC_DIR = .moc
OBJECTS_DIR = .obj
RCC_DIR = .rcc
