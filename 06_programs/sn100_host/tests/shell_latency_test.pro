QT += core gui widgets sql testlib
QT -= network
CONFIG += c++14 console testcase warn_on
CONFIG -= app_bundle debug
CONFIG += release
TEMPLATE = app
TARGET = shell_latency_test

# qmake is invoked against a frozen source snapshot. The shared shell pri also
# links the current history core required by its already-integrated interface.
isEmpty(SN100_LATENCY_GENERATED): error("Pass SN100_LATENCY_GENERATED as an absolute generated-header directory")
!exists($$SN100_LATENCY_GENERATED/latency_support.h): error("Missing frozen latency_support.h; run shell_latency.sh build")
INCLUDEPATH += $$PWD/.. $$PWD/../ui $$SN100_LATENCY_GENERATED
SOURCES += $$PWD/shell_latency_test.cpp
include(../shell/shell.pri)

MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
RCC_DIR = .rcc
