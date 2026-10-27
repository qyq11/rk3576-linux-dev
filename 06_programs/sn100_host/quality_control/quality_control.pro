QT += core gui widgets sql
QT -= network
CONFIG += c++14 warn_on
TEMPLATE = app
TARGET = sn100_qc_local
include(qc_standalone.pri)
SOURCES += main.cpp
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
RCC_DIR = .rcc
