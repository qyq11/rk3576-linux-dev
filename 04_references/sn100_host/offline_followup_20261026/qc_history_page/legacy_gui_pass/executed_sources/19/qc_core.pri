QT += core sql
INCLUDEPATH += $$PWD
SOURCES += $$PWD/qc_store.cpp $$PWD/qc_history_core.cpp $$PWD/../quality/statistics.cpp
HEADERS += $$PWD/qc_store.h $$PWD/qc_history_core.h $$PWD/qc_history_reply.h \
           $$PWD/../quality/statistics.h
