QT += core
QT -= gui
CONFIG += console c++14
CONFIG -= app_bundle
TEMPLATE = app
TARGET = sn100_clot_endpoint
SOURCES += main.cpp clot_endpoint.cpp
HEADERS += clot_endpoint.h
