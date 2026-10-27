QT += core
QT -= gui network
CONFIG += console c++14 warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = qc_history_core_test
isEmpty(SN100_FORMAL_ROOT): SN100_FORMAL_ROOT = $$PWD/..
!exists($$SN100_FORMAL_ROOT/quality/statistics.cpp): error(Set SN100_FORMAL_ROOT to the formal sn100_host directory)
INCLUDEPATH += $$PWD/../quality_control $$SN100_FORMAL_ROOT/quality_control
SOURCES += $$PWD/qc_history_core_test.cpp $$PWD/../quality_control/qc_history_core.cpp \
           $$SN100_FORMAL_ROOT/quality/statistics.cpp
HEADERS += $$PWD/../quality_control/qc_history_core.h $$SN100_FORMAL_ROOT/quality/statistics.h
OBJECTS_DIR = .obj
MOC_DIR = .moc
