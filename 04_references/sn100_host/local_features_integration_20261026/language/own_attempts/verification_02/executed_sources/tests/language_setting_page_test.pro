QT += core gui widgets testlib
QT -= network
CONFIG += c++14 console testcase warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = language_setting_page_test
INCLUDEPATH += $$PWD/.. $$PWD/../ui
SOURCES += language_setting_page_test.cpp ../ui/language_setting_page.cpp \
           ../ui/cusmessagebox.cpp ../ui/cusdialogmask.cpp
HEADERS += ../ui/language_setting_page.h ../ui/cusmessagebox.h ../ui/cusdialogmask.h
FORMS += ../ui/frmlanguagesetting.ui ../ui/cusmessagebox.ui ../ui/cusdialogmask.ui
RESOURCES += ../resources.qrc
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
RCC_DIR = .rcc
