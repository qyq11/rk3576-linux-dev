QT += core gui widgets sql testlib
QT -= network
CONFIG += c++14 console testcase warn_on
TEMPLATE = app
TARGET = calibration_page_test
include(../calibration.pri)
include(../../projects/projects_widgets.pri)
SOURCES += calibration_page_test.cpp
HEADERS += calibration_fixture.h
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
RCC_DIR = .rcc
