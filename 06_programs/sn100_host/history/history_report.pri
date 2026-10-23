# Optional heavy report capability. The shell uses history_core.pri alone.
include($$PWD/report_engine.pri)
SOURCES += $$PWD/report_controller.cpp
HEADERS += $$PWD/report_controller.h
