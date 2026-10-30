# 仅用于交叉编译ARM64后在RK执行；不用电脑产品运行代替。
QT += core sql
QT -= gui
CONFIG += c++14 console warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = original_runtime_rk_driver
INCLUDEPATH += ../..
SOURCES += original_runtime_rk_driver.cpp \
    ../original_business_database.cpp ../original_ini.cpp \
    ../original_system_profile.cpp ../original_runtime_settings.cpp ../original_capacity_calibration.cpp
HEADERS += ../original_business_database.h ../original_ini.h \
    ../original_system_profile.h ../original_runtime_settings.h ../original_capacity_calibration.h
MOC_DIR = .moc
OBJECTS_DIR = .obj
