QT += core gui widgets sql
CONFIG += c++14 warn_on console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = sn100_report_renderer
include(report_engine.pri)
# report_document's formatting calls share the existing store functions;
# the helper never constructs HistoryStore or opens any database.
SOURCES += report_renderer.cpp report_pdf_mapping.cpp history_store.cpp
HEADERS += report_pdf_mapping.h history_store.h
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
RCC_DIR = .rcc
