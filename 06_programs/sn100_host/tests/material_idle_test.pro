QT += core gui widgets testlib
CONFIG += console testcase c++14
CONFIG -= app_bundle
TEMPLATE = app
TARGET = material_idle_test
INCLUDEPATH += $$PWD/.. $$PWD/../shell/widgets/material
SOURCES += $$PWD/material_idle_test.cpp \
    $$files($$PWD/../shell/widgets/material/*.cpp) \
    $$files($$PWD/../shell/widgets/material/lib/*.cpp)
HEADERS += $$files($$PWD/../shell/widgets/material/*.h) \
    $$files($$PWD/../shell/widgets/material/lib/*.h)
OBJECTS_DIR = .obj
MOC_DIR = .moc
