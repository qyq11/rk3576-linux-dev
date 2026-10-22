QT += core gui widgets
CONFIG += c++17
TEMPLATE = app
TARGET = qt_demo
SOURCES += app_main.cpp

# Build all intermediate files under the repository's temp/ directory.
OBJECTS_DIR = $$clean_path($$OUT_PWD/obj)
MOC_DIR = $$clean_path($$OUT_PWD/moc)
RCC_DIR = $$clean_path($$OUT_PWD/rcc)
UI_DIR = $$clean_path($$OUT_PWD/ui)
win32:CONFIG += console
win32:CONFIG -= app_bundle
msvc:QMAKE_CXXFLAGS += /utf-8
rk_board:DEFINES += RK_BOARD
