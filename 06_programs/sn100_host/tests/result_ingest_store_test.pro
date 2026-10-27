QT = core sql testlib
CONFIG += console c++14 release warn_on
CONFIG -= app_bundle debug
TEMPLATE = app
TARGET = result_ingest_store_test

# Keep the test's __FILE__ runtime fence absolute in actual compiler recipes.
QMAKE_PROJECT_DEPTH = 0
PROGRAM_ROOT = $$clean_path($$PWD/..)
INCLUDEPATH += $$PROGRAM_ROOT $$PROGRAM_ROOT/history $$PROGRAM_ROOT/projects
SOURCES += \
    $$PWD/result_ingest_store_test.cpp \
    $$PROGRAM_ROOT/history/history_store.cpp \
    $$PROGRAM_ROOT/history/history_value_text.cpp \
    $$PROGRAM_ROOT/history/report_remaining_store.cpp \
    $$PROGRAM_ROOT/history/result_ingest_store.cpp \
    $$PROGRAM_ROOT/history/result_project_references.cpp
HEADERS += \
    $$PROGRAM_ROOT/history/history_store.h \
    $$PROGRAM_ROOT/history/result_project_references.h

# Actual project validation/editor/CusCheckBox implementations and MOC are
# linked by shared support. Widgets are a link dependency; the unchanged test
# uses QCoreApplication and creates database workers only, no GUI instances.
include($$PROGRAM_ROOT/projects/projects_store_support.pri)
# No fixture qrc, private validation extraction, old objects or page entry.
