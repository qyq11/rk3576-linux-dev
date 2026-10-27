QT += core sql
INCLUDEPATH += $$PWD
include($$PWD/../samples/sample_qc_values.pri)
SOURCES += $$PWD/qc_store.cpp $$PWD/qc_sample_reference_guard.cpp \
           $$PWD/qc_history_core.cpp $$PWD/../quality/statistics.cpp
HEADERS += $$PWD/qc_store.h $$PWD/qc_history_core.h $$PWD/qc_history_reply.h \
           $$PWD/../quality/statistics.h
SOURCES = $$unique(SOURCES)
HEADERS = $$unique(HEADERS)
