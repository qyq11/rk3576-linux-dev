QT += core gui widgets sql svg testlib
CONFIG += c++14 testcase warn_on
TARGET = sample_reset_workflow_test
include(../samples_core.pri)
include(../samples_standalone.pri)
SOURCES += sample_reset_workflow_test.cpp
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
RCC_DIR = .rcc
