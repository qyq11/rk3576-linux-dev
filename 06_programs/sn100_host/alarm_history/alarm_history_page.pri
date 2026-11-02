QT += core gui widgets sql
INCLUDEPATH += $$PWD $$PWD/calendar $$PWD/.. $$PWD/../ui \
               $$PWD/../samples/material $$PWD/../shell/widgets/material
SOURCES += $$PWD/alarm_history_page.cpp $$PWD/alarm_history_host.cpp
SOURCES *= $$clean_path($$PWD/calendar/cuscalendar.cpp)
HEADERS += $$PWD/alarm_history_page.h $$PWD/alarm_history_host.h
HEADERS *= $$clean_path($$PWD/calendar/cuscalendar.h)
FORMS += $$PWD/maskdlgabnormal.ui
FORMS *= $$clean_path($$PWD/cusmaskcalendar.ui) $$clean_path($$PWD/calendar/cuscalendar.ui)
# Close/message icons and calendar arrows are supplied by existing shared
# account_resources.qrc and qc_history_resources.qrc in the unified shell.
RESOURCES += $$PWD/alarm_history_resources.qrc
