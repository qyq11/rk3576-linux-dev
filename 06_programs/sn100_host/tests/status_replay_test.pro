QT += core testlib
QT -= gui network
CONFIG += console c++14 testcase warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = status_replay_test
INCLUDEPATH += $$PWD/../protocol
SOURCES += status_replay_test.cpp \
           ../protocol/status_replay.cpp ../protocol/status_session.cpp ../protocol/frame_codec.cpp
HEADERS += ../protocol/status_replay.h ../protocol/status_session.h ../protocol/frame_codec.h
MOC_DIR = .moc
OBJECTS_DIR = .obj
