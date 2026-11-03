# UI dependencies (CusView / Material buttons / original dialogs) are shared by shell.pri.
include($$PWD/maintenance_core.pri)
include($$PWD/device_control.pri)
INCLUDEPATH += $$PWD
SOURCES += $$PWD/instrument_page.cpp $$PWD/original_status_values.cpp
HEADERS += $$PWD/instrument_page.h $$PWD/original_status_values.h
SOURCES += $$PWD/original_channel_receiver.cpp $$PWD/original_channel_store.cpp
HEADERS += $$PWD/original_channel_receiver.h $$PWD/original_channel_store.h
FORMS += $$PWD/frmdriverset.ui
RESOURCES += $$PWD/instrument_resources.qrc
