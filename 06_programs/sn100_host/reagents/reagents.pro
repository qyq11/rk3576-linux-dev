QT += core gui widgets sql
QT -= network
CONFIG += c++14 warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = sn100_reagents
include(reagents.pri)
include(../projects/projects_core.pri)
SOURCES += main.cpp ../ui/cusdialogmask.cpp ../ui/cusmessagebox.cpp \
           $$files(../shell/widgets/material/*.cpp) $$files(../shell/widgets/material/lib/*.cpp) ../shell/widgets/cusview.cpp
HEADERS += ../ui/cusdialogmask.h ../ui/cusmessagebox.h \
           $$files(../shell/widgets/material/*.h) $$files(../shell/widgets/material/lib/*.h) ../shell/widgets/cusview.h
FORMS += ../ui/cusdialogmask.ui ../ui/cusmessagebox.ui
RESOURCES += ../shell/account_resources.qrc
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
RCC_DIR = .rcc
