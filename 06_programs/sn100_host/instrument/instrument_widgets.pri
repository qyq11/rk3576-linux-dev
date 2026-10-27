# UI dependencies (CusView / Material buttons / original dialogs) are shared by shell.pri.
include($$PWD/maintenance_core.pri)
include($$PWD/device_control.pri)
INCLUDEPATH += $$PWD
SOURCES += $$PWD/instrument_page.cpp
HEADERS += $$PWD/instrument_page.h
FORMS += $$PWD/frmdriverset.ui
RESOURCES += $$PWD/instrument_resources.qrc
