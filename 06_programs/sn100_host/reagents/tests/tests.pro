QT += core gui widgets sql testlib
QT -= network
CONFIG += c++14 warn_on testcase
CONFIG -= app_bundle
TEMPLATE = app
TARGET = test_reagents
include(../reagents.pri)
include(../../projects/projects_core.pri)
SOURCES += $$PWD/../../ui/cusdialogmask.cpp $$PWD/../../ui/cusmessagebox.cpp \
           $$files($$PWD/../../shell/widgets/material/*.cpp) $$files($$PWD/../../shell/widgets/material/lib/*.cpp) $$PWD/../../shell/widgets/cusview.cpp
HEADERS += $$PWD/../../ui/cusdialogmask.h $$PWD/../../ui/cusmessagebox.h \
           $$files($$PWD/../../shell/widgets/material/*.h) $$files($$PWD/../../shell/widgets/material/lib/*.h) $$PWD/../../shell/widgets/cusview.h
FORMS += $$PWD/../../ui/cusdialogmask.ui $$PWD/../../ui/cusmessagebox.ui
RESOURCES += $$PWD/../../shell/account_resources.qrc
SOURCES += $$PWD/test_reagents.cpp
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
RCC_DIR = .rcc
