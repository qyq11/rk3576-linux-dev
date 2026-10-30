QT += core sql testlib
QT -= gui
CONFIG += console c++14 testcase
TEMPLATE = app
TARGET = original_result_codec_rk_test
INCLUDEPATH += .. ../..
SOURCES += ../original_result_codec.cpp original_result_codec_rk_test.cpp
