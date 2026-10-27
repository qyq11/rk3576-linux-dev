QT += core widgets sql testlib
CONFIG += c++14 console testcase warn_on
TARGET = qc_history_store_test
# 仅复用正式历史/统计输入；运行必须显式SN100_QC_HISTORY_TEST_ROOT。
INCLUDEPATH += .. ../quality_control
include(../quality_control/qc_core.pri)
SOURCES += qc_history_store_test.cpp ../projects/projects_store.cpp \
    ../projects/project_advanced.cpp ../projects/cuscheckbox.cpp
HEADERS += ../projects/projects_store.h ../projects/project_advanced.h ../projects/cuscheckbox.h
MOC_DIR = .moc
OBJECTS_DIR = .obj
