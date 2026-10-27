# Stable history query/statistics component for the independent entry and shell.
# Template/PDF rendering is deliberately linked by history_report.pri separately.
QT += core gui widgets sql
INCLUDEPATH += $$PWD
include($$PWD/../projects/projects_store_support.pri)
SOURCES += $$PWD/history_store.cpp $$PWD/history_page.cpp $$PWD/result_statistics.cpp
SOURCES += $$PWD/history_value_text.cpp $$PWD/result_ingest_store.cpp $$PWD/result_project_references.cpp
HEADERS += $$PWD/result_project_references.h
SOURCES += $$PWD/report_remaining_store.cpp $$PWD/report_remaining_display_dialog.cpp
HEADERS += $$PWD/report_remaining_view.h $$PWD/report_remaining_display_dialog.h
FORMS += $$PWD/mask_dlg_display_setting.ui
HEADERS += $$PWD/history_store.h $$PWD/history_page.h $$PWD/result_statistics.h
FORMS += $$PWD/frm_report_material.ui $$PWD/mask_dlg_query_report.ui \
         $$PWD/mask_dlg_report_detail.ui $$PWD/mask_dlg_export.ui
RESOURCES += $$PWD/resources.qrc
include(report_dictionary.pri)
