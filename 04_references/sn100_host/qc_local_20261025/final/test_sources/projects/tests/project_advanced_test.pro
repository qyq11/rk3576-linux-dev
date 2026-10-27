QT += core gui widgets sql testlib
CONFIG += c++14 testcase warn_on
TARGET = project_advanced_test
include(../projects_core.pri)
include(../projects_widgets.pri)
SOURCES += project_advanced_test.cpp
RESOURCES += ../projects_standalone_resources.qrc
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
RCC_DIR = .rcc
