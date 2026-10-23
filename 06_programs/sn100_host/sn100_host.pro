QT += core gui widgets sql
QT -= network
CONFIG += c++14 warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = sn100_host
INCLUDEPATH += $$PWD $$PWD/ui
SOURCES += main.cpp \
           data/hospital_store.cpp \
           ui/frmhospinfosetting.cpp \
           ui/mnemonic.cpp
SOURCES += data/account_store.cpp ui/frmaccountsetting.cpp
SOURCES += ui/cusmessagebox.cpp ui/cusdialogmask.cpp
HEADERS += data/hospital_store.h \
           ui/frmhospinfosetting.h \
           ui/mnemonic.h
HEADERS += data/account_store.h ui/frmaccountsetting.h
HEADERS += ui/cusmessagebox.h ui/cusdialogmask.h
FORMS += ui/frmhospinfosetting.ui ui/frmaccountsetting.ui
FORMS += ui/cusmessagebox.ui ui/cusdialogmask.ui
RESOURCES += resources.qrc
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
RCC_DIR = .rcc

