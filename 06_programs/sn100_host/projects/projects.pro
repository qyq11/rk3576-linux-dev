QT += core gui widgets sql
QT -= network
CONFIG += c++14 warn_on
TEMPLATE = app
TARGET = sn100_projects
include(projects_core.pri)
include(projects_widgets.pri)
SOURCES += main.cpp
RESOURCES += projects_standalone_resources.qrc
# 原复制卡的错误提示沿用已有提示卡；统一主窗另有自己的归属。
SOURCES += ../ui/cusmessagebox.cpp ../ui/cusdialogmask.cpp
HEADERS += ../ui/cusmessagebox.h ../ui/cusdialogmask.h
FORMS += ../ui/cusmessagebox.ui ../ui/cusdialogmask.ui
INCLUDEPATH += ../ui
RESOURCES += ../shell/account_resources.qrc
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
RCC_DIR = .rcc
