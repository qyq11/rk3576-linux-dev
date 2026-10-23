QT += core testlib
QT -= gui network
CONFIG += console testcase c++14 warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = sn100_read_only_messages_test
INCLUDEPATH += ../protocol
SOURCES += read_only_messages_test.cpp \
    ../protocol/frame_codec.cpp \
    ../protocol/status_session.cpp \
    ../protocol/read_only_messages.cpp
HEADERS += ../protocol/frame_codec.h \
    ../protocol/status_session.h \
    ../protocol/read_only_messages.h
DISTFILES += fixtures/protocol_diagnostics.json
MOC_DIR = .moc
OBJECTS_DIR = .obj
