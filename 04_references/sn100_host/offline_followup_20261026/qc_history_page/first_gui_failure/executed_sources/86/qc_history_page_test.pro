QT += core gui widgets sql testlib
QT -= network
CONFIG += console c++14 testcase warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = qc_history_page_test
isEmpty(SN100_FORMAL_ROOT): SN100_FORMAL_ROOT = $$PWD/..
!exists($$SN100_FORMAL_ROOT/quality/statistics.cpp): error(Set SN100_FORMAL_ROOT to the formal sn100_host directory)
INCLUDEPATH += $$SN100_FORMAL_ROOT $$SN100_FORMAL_ROOT/quality_control
SOURCES += $$PWD/qc_history_page_test.cpp
HEADERS += $$PWD/qc_history_gui_fixture.h
include($$SN100_FORMAL_ROOT/quality_control/qc_standalone.pri)
OBJECTS_DIR = .obj
MOC_DIR = .moc
UI_DIR = .ui
RCC_DIR = .rcc
