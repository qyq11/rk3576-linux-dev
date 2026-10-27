QT += core sql
QT -= gui
CONFIG += console c++14 warn_on
CONFIG -= app_bundle
TARGET = sn100_original_files_check
SOURCES += check.cpp original_ini.cpp
HEADERS += original_ini.h
