QT += core sql testlib
CONFIG += c++14 testcase console warn_on
TARGET = qc_local_store_test
include(../quality_control/qc_core.pri)
SOURCES += qc_local_store_test.cpp
MOC_DIR = .moc
OBJECTS_DIR = .obj
