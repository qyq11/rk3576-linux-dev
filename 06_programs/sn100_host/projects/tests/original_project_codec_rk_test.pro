QT += core testlib
QT -= gui
CONFIG += console c++14 testcase
TEMPLATE = app
TARGET = original_project_codec_rk_test
INCLUDEPATH += .. ../..
SOURCES += ../original_project_codec.cpp original_project_codec_rk_test.cpp
