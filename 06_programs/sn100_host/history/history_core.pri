# Stable history query/statistics component for the independent entry and shell.
# Template/PDF rendering is deliberately linked by history_report.pri separately.
QT += core gui widgets sql
INCLUDEPATH += $$PWD
SOURCES += $$PWD/history_store.cpp $$PWD/history_page.cpp $$PWD/result_statistics.cpp
HEADERS += $$PWD/history_store.h $$PWD/history_page.h $$PWD/result_statistics.h
FORMS += $$PWD/frm_report_material.ui $$PWD/mask_dlg_query_report.ui \
         $$PWD/mask_dlg_report_detail.ui $$PWD/mask_dlg_export.ui
RESOURCES += $$PWD/resources.qrc
