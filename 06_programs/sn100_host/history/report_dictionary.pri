# Report-detail read-only hospital dictionary picker.
QT += core gui widgets
INCLUDEPATH += $$PWD
SOURCES += $$PWD/report_dictionary_dialog.cpp
HEADERS += $$PWD/report_dictionary_dialog.h $$PWD/../data/hospital_dictionary_query.h
FORMS += $$PWD/mask_dlg_select.ui
RESOURCES += $$PWD/report_dictionary_resources.qrc
