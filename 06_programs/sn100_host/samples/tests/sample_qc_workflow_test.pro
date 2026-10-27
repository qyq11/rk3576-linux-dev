QT += core gui widgets sql svg testlib
CONFIG += c++14 testcase warn_on
TARGET = sample_qc_workflow_test
include(../samples_core.pri)
include(../samples_standalone.pri)
include(../../quality_control/qc_core.pri)
SOURCES += sample_qc_workflow_test.cpp ../../shell/sample_qc_shell.cpp
HEADERS += ../../shell/sample_qc_shell.h
SOURCES = $$unique(SOURCES)
HEADERS = $$unique(HEADERS)
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
RCC_DIR = .rcc
