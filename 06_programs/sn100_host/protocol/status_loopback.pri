QT += core network
INCLUDEPATH += $$PWD
# Consumer also supplies the existing frame_codec.cpp/status_session.cpp closure.
SOURCES += $$PWD/status_loopback.cpp $$PWD/read_only_client.cpp $$PWD/tcp_transport.cpp $$PWD/read_only_messages.cpp
HEADERS += $$PWD/status_loopback.h $$PWD/read_only_client.h $$PWD/tcp_transport.h $$PWD/read_only_messages.h
