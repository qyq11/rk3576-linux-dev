QT += core gui widgets sql svg testlib
CONFIG += c++14 testcase warn_on
TARGET = samples_test
include(../samples_core.pri)
include(../samples_standalone.pri)
SOURCES += samples_test.cpp
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
RCC_DIR = .rcc

