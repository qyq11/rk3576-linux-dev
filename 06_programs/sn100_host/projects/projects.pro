QT += core gui widgets sql
QT -= network
CONFIG += c++14 warn_on
TEMPLATE = app
TARGET = sn100_projects
include(projects_core.pri)
include(projects_widgets.pri)
SOURCES += main.cpp
RESOURCES += projects_standalone_resources.qrc
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
RCC_DIR = .rcc
