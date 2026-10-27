QT += core gui widgets svg testlib
CONFIG += c++14 console testcase warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = lis_settings_test
INCLUDEPATH += $$PWD/.. $$PWD/../ui $$PWD/../projects $$PWD/../samples/material $$PWD/../shell/widgets/material
SOURCES += lis_settings_test.cpp $$PWD/../projects/cuscheckbox.cpp \
    $$PWD/../samples/material/qtmaterialradiobutton.cpp \
    $$PWD/../samples/material/lib/qtmaterialcheckable.cpp \
    $$PWD/../samples/material/lib/qtmaterialcheckable_internal.cpp \
    $$files($$PWD/../shell/widgets/material/lib/*.cpp)
HEADERS += $$PWD/../projects/cuscheckbox.h \
    $$PWD/../samples/material/qtmaterialradiobutton.h \
    $$PWD/../samples/material/lib/qtmaterialcheckable.h \
    $$PWD/../samples/material/lib/qtmaterialcheckable_internal.h \
    $$files($$PWD/../shell/widgets/material/lib/*.h)
RESOURCES += $$PWD/../samples/samples_resources.qrc $$PWD/../projects/projects_resources.qrc $$PWD/../shell/account_resources.qrc
include(../ui/lis_settings.pri)
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
