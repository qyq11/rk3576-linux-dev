# Real formal HospitalStore query API and shared DTO, with the frozen SQL assertions.
QT += core sql testlib
QT -= gui
CONFIG += c++14 console testcase warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = report_dictionary_store_contract_test
INCLUDEPATH += $$PWD/..
SOURCES += $$PWD/report_dictionary_store_contract_test.cpp $$PWD/../data/hospital_store.cpp
HEADERS += $$PWD/../data/hospital_store.h $$PWD/../data/hospital_dictionary_query.h
MOC_DIR = .moc
OBJECTS_DIR = .obj
