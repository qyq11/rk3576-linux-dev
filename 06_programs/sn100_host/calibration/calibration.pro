QT += core gui widgets sql
QT -= network
CONFIG += c++14 warn_on
TEMPLATE = app
TARGET = sn100_calibration
include(calibration.pri)
include(../projects/projects_widgets.pri)
SOURCES += main.cpp
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
RCC_DIR = .rcc
