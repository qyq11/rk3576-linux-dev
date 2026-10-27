QT += core network testlib
QT -= gui
CONFIG += console testcase c++14 warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = sn100_protocol_peer_gate_test

# Compile the current real implementation; do not copy a client state machine.
PROGRAM = $$clean_path($$PWD/../../../../06_programs/sn100_host)
SOURCES += $$PWD/protocol_peer_gate_test.cpp \
           $$PROGRAM/protocol/read_only_client.cpp \
           $$PROGRAM/protocol/tcp_transport.cpp \
           $$PROGRAM/protocol/read_only_messages.cpp \
           $$PROGRAM/protocol/frame_codec.cpp \
           $$PROGRAM/protocol/status_session.cpp
HEADERS += $$PROGRAM/protocol/read_only_client.h \
           $$PROGRAM/protocol/tcp_transport.h \
           $$PROGRAM/protocol/read_only_messages.h \
           $$PROGRAM/protocol/frame_codec.h \
           $$PROGRAM/protocol/status_session.h
INCLUDEPATH += $$PROGRAM/protocol
MOC_DIR = .moc
OBJECTS_DIR = .obj
