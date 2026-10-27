QT += core widgets sql testlib
CONFIG += c++14 console testcase warn_on
TARGET = calibration_reference_test
INCLUDEPATH += .. ../calibration ../quality_control
SOURCES += calibration_reference_test.cpp \
    ../calibration/calibration_store.cpp ../projects/projects_store.cpp \
    ../projects/project_advanced.cpp ../projects/cuscheckbox.cpp \
    ../reagents/reagent_store.cpp ../quality_control/qc_store.cpp
HEADERS += ../calibration/calibration_store.h ../calibration/calibration_types.h \
    ../projects/projects_store.h ../projects/project_advanced.h ../projects/cuscheckbox.h ../reagents/reagent_store.h \
    ../reagents/reagent_types.h ../quality_control/qc_store.h
MOC_DIR = .moc
OBJECTS_DIR = .obj
