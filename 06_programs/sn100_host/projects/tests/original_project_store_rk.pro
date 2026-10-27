# 本目标只交叉编译ARM64，上传RK运行。
QT += core gui widgets sql
CONFIG += c++14 console warn_on
CONFIG -= app_bundle
TARGET = original_project_store_rk_driver
include(../projects_store_support.pri)
SOURCES += original_project_store_rk_driver.cpp
MOC_DIR = .moc
OBJECTS_DIR = .obj
