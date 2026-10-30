# 仅交叉编译ARM64并上传RK运行；匿名原表只在显式新测试目录生成。
QT += core sql network
QT -= gui
CONFIG += c++14 console warn_on
CONFIG -= app_bundle
TARGET = original_management_rk_driver
INCLUDEPATH += .. ../data ../instrument ../alarm_history ../original_files
SOURCES += original_management_rk_driver.cpp \
    ../data/hospital_store.cpp ../data/account_store.cpp \
    ../instrument/maintenance_store.cpp ../alarm_history/alarm_history_store.cpp \
    ../alarm_history/original_alarm_events.cpp ../original_files/original_ini.cpp \
    ../original_files/original_business_database.cpp ../original_files/original_system_profile.cpp \
    ../protocol/read_only_messages.cpp ../protocol/status_session.cpp ../protocol/frame_codec.cpp
HEADERS += ../data/hospital_store.h ../data/account_store.h \
    ../instrument/maintenance_store.h ../alarm_history/alarm_history_store.h \
    ../alarm_history/original_alarm_events.h ../original_files/original_ini.h \
    ../original_files/original_business_database.h ../original_files/original_system_profile.h
MOC_DIR = .moc
OBJECTS_DIR = .obj
