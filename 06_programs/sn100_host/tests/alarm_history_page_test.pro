QT += core gui widgets sql testlib
QT -= network
CONFIG += console c++14 testcase warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = alarm_history_page_test
isEmpty(SN100_FORMAL_ROOT): SN100_FORMAL_ROOT = $$PWD/..
!exists($$SN100_FORMAL_ROOT/alarm_history/alarm_history_store.cpp): error(Formal AlarmHistory Store is required)
include($$SN100_FORMAL_ROOT/alarm_history/alarm_history_core.pri)
include($$SN100_FORMAL_ROOT/alarm_history/alarm_history_page.pri)
INCLUDEPATH += $$SN100_FORMAL_ROOT/shell/widgets/material/lib
SOURCES += $$PWD/alarm_history_page_test.cpp \
    $$SN100_FORMAL_ROOT/ui/cusdialogmask.cpp $$SN100_FORMAL_ROOT/ui/cusmessagebox.cpp \
    $$SN100_FORMAL_ROOT/samples/material/qtmaterialscrollbar.cpp \
    $$SN100_FORMAL_ROOT/samples/material/qtmaterialscrollbar_internal.cpp
HEADERS += $$SN100_FORMAL_ROOT/ui/cusdialogmask.h $$SN100_FORMAL_ROOT/ui/cusmessagebox.h \
    $$SN100_FORMAL_ROOT/samples/material/qtmaterialscrollbar.h \
    $$SN100_FORMAL_ROOT/samples/material/qtmaterialscrollbar_p.h \
    $$SN100_FORMAL_ROOT/samples/material/qtmaterialscrollbar_internal.h
FORMS += $$SN100_FORMAL_ROOT/ui/cusdialogmask.ui $$SN100_FORMAL_ROOT/ui/cusmessagebox.ui
RESOURCES += $$SN100_FORMAL_ROOT/shell/account_resources.qrc \
    $$SN100_FORMAL_ROOT/quality_control/qc_history_resources.qrc
include($$SN100_FORMAL_ROOT/projects/projects_widgets.pri)
OBJECTS_DIR = .obj
MOC_DIR = .moc
UI_DIR = .ui
RCC_DIR = .rcc
