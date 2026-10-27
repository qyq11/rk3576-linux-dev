QT += core gui widgets sql
CONFIG += c++14 warn_on console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = sn100_report_renderer
include(report_engine.pri)
# The helper uses only value formatting; it never constructs HistoryStore.
SOURCES += report_renderer.cpp report_pdf_mapping.cpp history_value_text.cpp
HEADERS += report_pdf_mapping.h report_remaining_view.h
MOC_DIR = .moc
UI_DIR = .ui
OBJECTS_DIR = .obj
RCC_DIR = .rcc
