QT += network
INCLUDEPATH += $$PWD/..
SOURCES += $$PWD/lis_mllp_codec.cpp $$PWD/lis_tcp_transport.cpp
HEADERS += $$PWD/lis_mllp_codec.h $$PWD/lis_tcp_transport.h
SOURCES += $$PWD/lis_connection_controller.cpp
HEADERS += $$PWD/lis_connection_controller.h $$PWD/lis_connection_values.h
