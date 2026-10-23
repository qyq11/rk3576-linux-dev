# Shared, original-template QtRPT engine for the UI and isolated PDF helper.
INCLUDEPATH += $$PWD
SOURCES += $$PWD/report_document.cpp $$PWD/report_render.cpp
HEADERS += $$PWD/report_document.h $$PWD/report_render.h
RESOURCES += $$PWD/report_resources.qrc
include($$PWD/../thirdparty/qtrpt/qtrpt.pri)
