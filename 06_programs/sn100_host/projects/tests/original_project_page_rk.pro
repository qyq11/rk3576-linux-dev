# 只交叉编译给RK；读取专用原配置副本，使用正式页面验证保存和失败恢复。
QT += core gui widgets sql
CONFIG += c++14 console warn_on
CONFIG -= app_bundle
TARGET = original_project_page_rk_driver
include(../projects_core.pri)
include(../projects_widgets.pri)
SOURCES += original_project_page_rk_driver.cpp ../../reagents/reagent_store.cpp
HEADERS += ../../reagents/reagent_store.h ../../reagents/reagent_types.h
RESOURCES += ../projects_standalone_resources.qrc
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
RCC_DIR = .rcc
