QT += core sql testlib
QT -= gui
CONFIG += console c++14 testcase
TEMPLATE = app
TARGET = original_task_codec_rk_test
INCLUDEPATH += .. ../..
SOURCES += ../original_task_codec.cpp original_task_codec_rk_test.cpp
