QT += core sql testlib
QT -= gui network
CONFIG += c++14 console testcase warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = maintenance_store_test
SOURCES += $$PWD/maintenance_store_test.cpp
include($$PWD/../instrument/maintenance_core.pri)
MOC_DIR = .moc
OBJECTS_DIR = .obj
