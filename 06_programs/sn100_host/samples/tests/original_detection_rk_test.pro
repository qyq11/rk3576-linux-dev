QT += core sql testlib widgets
CONFIG += console c++14 testcase
TEMPLATE = app
TARGET = original_detection_rk_test
INCLUDEPATH += .. ../.. ../../projects
SOURCES += original_detection_rk_test.cpp \
    ../original_task_codec.cpp ../original_result_codec.cpp ../original_sample_repository.cpp ../original_detection_session.cpp \
    ../../original_files/original_business_database.cpp ../../original_files/original_ini.cpp \
    ../../reagents/original_reagent_repository.cpp ../../projects/original_project_repository.cpp \
    ../../projects/project_advanced.cpp ../../projects/cuscheckbox.cpp ../sample_task_reset.cpp
HEADERS += ../../projects/project_advanced.h ../../projects/cuscheckbox.h
