QT += core network testlib
QT -= gui
CONFIG += console testcase c++14 warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = sn100_read_only_client_test
SOURCES += read_only_client_test.cpp \
           ../protocol/read_only_client.cpp \
           ../protocol/tcp_transport.cpp \
           ../protocol/read_only_messages.cpp \
           ../protocol/frame_codec.cpp \
           ../protocol/status_session.cpp
HEADERS += ../protocol/read_only_client.h \
           ../protocol/tcp_transport.h \
           ../protocol/read_only_messages.h \
           ../protocol/frame_codec.h \
           ../protocol/status_session.h
INCLUDEPATH += ../protocol
MOC_DIR = .moc
OBJECTS_DIR = .obj
RCC_DIR = .rcc
