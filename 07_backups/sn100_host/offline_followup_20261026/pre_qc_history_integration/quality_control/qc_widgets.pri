QT += core gui widgets sql
INCLUDEPATH += $$PWD $$PWD/../samples/material $$PWD/.. $$PWD/../ui $$PWD/../projects \
               $$PWD/../shell/widgets $$PWD/../shell/widgets/material
SOURCES += $$PWD/quality_control_page.cpp
HEADERS += $$PWD/quality_control_page.h
FORMS += $$PWD/frm_quality.ui $$PWD/mask_dlg_add_control.ui \
         $$PWD/mask_dlg_control_batch_setting.ui $$PWD/quality_control_add_wgt.ui \
         $$PWD/quality_control_item_add_project_wgt.ui
