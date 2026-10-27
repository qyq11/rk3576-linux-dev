# 在RK显示环境驱动正式项目页面，单独匿名目录，不发送仪器动作。
QT += core gui widgets sql
CONFIG += c++14 console warn_on
CONFIG -= app_bundle
TARGET = project_config_page_rk_driver
include(../projects_core.pri)
include(../projects_widgets.pri)
SOURCES += project_config_page_rk_driver.cpp ../../reagents/reagent_store.cpp
HEADERS += ../../reagents/reagent_store.h ../../reagents/reagent_types.h
RESOURCES += ../projects_standalone_resources.qrc
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
RCC_DIR = .rcc
