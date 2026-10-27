# 本目标用于ARM64交叉构建并上传RK运行；不安排电脑业务测试。
QT += core gui widgets sql
CONFIG += c++14 console warn_on
CONFIG -= app_bundle
TARGET = project_config_store_rk_driver
include(../projects_store_support.pri)
SOURCES += project_config_store_rk_driver.cpp
MOC_DIR = .moc
OBJECTS_DIR = .obj
