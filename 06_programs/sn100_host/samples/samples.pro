QT += core gui widgets sql svg
CONFIG += c++14 warn_on
TEMPLATE = app
TARGET = sn100_samples
include(samples_core.pri)
include(samples_standalone.pri)
SOURCES += main.cpp
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
RCC_DIR = .rcc
