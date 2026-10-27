SOURCES += $$PWD/frmlissetting.cpp $$PWD/../data/lis_settings_store.cpp $$PWD/../data/serial_port_catalog.cpp
HEADERS += $$PWD/frmlissetting.h $$PWD/../data/lis_settings_store.h $$PWD/../data/serial_port_catalog.h
FORMS += $$PWD/frmlissetting.ui
win32:LIBS += -ladvapi32
