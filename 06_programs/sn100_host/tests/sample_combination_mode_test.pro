QT += core gui widgets sql svg testlib
CONFIG += c++14 testcase warn_on
TARGET = sample_combination_mode_test
include($$PWD/../samples/samples_core.pri)
include($$PWD/../samples/samples_standalone.pri)
SOURCES += $$PWD/sample_combination_mode_test.cpp
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
RCC_DIR = .rcc
