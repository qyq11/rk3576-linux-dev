QT += core gui widgets sql testlib
QT -= network
CONFIG += c++14 console testcase warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = offline_test
INCLUDEPATH += $$PWD/..
SOURCES += offline_test.cpp \
           ../data/hospital_store.cpp \
           ../ui/frmhospinfosetting.cpp \
           ../ui/mnemonic.cpp
HEADERS += ../data/hospital_store.h \
           ../ui/frmhospinfosetting.h \
           ../ui/mnemonic.h
FORMS += ../ui/frmhospinfosetting.ui
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
