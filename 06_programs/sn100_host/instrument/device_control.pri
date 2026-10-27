isEmpty(SN100_DEVICE_CONTROL_INCLUDED) {
    SN100_DEVICE_CONTROL_INCLUDED = 1
    QT += network
    SOURCES *= $$PWD/device_control.cpp $$PWD/original_device_commands.cpp \
               $$PWD/../protocol/tcp_transport.cpp $$PWD/../protocol/frame_codec.cpp \
               $$PWD/../protocol/read_only_messages.cpp $$PWD/../protocol/status_session.cpp
    HEADERS *= $$PWD/device_control.h $$PWD/original_device_commands.h \
               $$PWD/../protocol/tcp_transport.h $$PWD/../protocol/frame_codec.h \
               $$PWD/../protocol/read_only_messages.h $$PWD/../protocol/status_session.h
}
