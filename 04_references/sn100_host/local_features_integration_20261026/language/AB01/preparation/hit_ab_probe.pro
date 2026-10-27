QT += core gui widgets testlib
QT -= network
CONFIG += console c++14 testcase warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = hit_ab_probe
SN100_ROOT = $$PWD/../../../../06_programs/sn100_host
INCLUDEPATH += $$SN100_ROOT $$SN100_ROOT/ui
SOURCES += $$PWD/hit_ab_probe.cpp $$SN100_ROOT/ui/language_setting_page.cpp \
           $$SN100_ROOT/ui/cusmessagebox.cpp $$SN100_ROOT/ui/cusdialogmask.cpp
HEADERS += $$SN100_ROOT/ui/language_setting_page.h $$SN100_ROOT/ui/cusmessagebox.h $$SN100_ROOT/ui/cusdialogmask.h
FORMS += $$SN100_ROOT/ui/frmlanguagesetting.ui $$SN100_ROOT/ui/cusmessagebox.ui $$SN100_ROOT/ui/cusdialogmask.ui
RESOURCES += $$SN100_ROOT/resources.qrc
OBJECTS_DIR = .obj
MOC_DIR = .moc
UI_DIR = .ui
RCC_DIR = .rcc
