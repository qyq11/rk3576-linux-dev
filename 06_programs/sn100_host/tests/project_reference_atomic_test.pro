QT += core widgets sql testlib
CONFIG += c++14 console testcase warn_on
TARGET = project_reference_atomic_test
INCLUDEPATH += ..
SOURCES += project_reference_atomic_test.cpp ../projects/projects_store.cpp \
    ../projects/project_advanced.cpp ../projects/cuscheckbox.cpp ../quality_control/qc_store.cpp
HEADERS += ../projects/projects_store.h ../projects/project_advanced.h \
    ../projects/cuscheckbox.h ../quality_control/qc_store.h
MOC_DIR = .moc
OBJECTS_DIR = .obj
