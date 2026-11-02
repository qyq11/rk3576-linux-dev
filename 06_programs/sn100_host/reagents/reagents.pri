QT += widgets sql
INCLUDEPATH += $$PWD $$PWD/.. $$PWD/../projects $$PWD/../ui $$PWD/../shell/widgets $$PWD/../shell/widgets/material
SOURCES += $$PWD/reagent_store.cpp $$PWD/reagents_page.cpp $$PWD/wgtreagentpanel.cpp $$PWD/cusreagenthole.cpp
HEADERS += $$PWD/reagent_types.h $$PWD/reagent_store.h $$PWD/reagents_page.h $$PWD/wgtreagentpanel.h $$PWD/cusreagenthole.h
FORMS += $$PWD/frmreagentmaterial.ui $$PWD/wgtreagentpanel.ui $$PWD/maskdlgreagentlistsetting.ui $$PWD/maskdlgreagentsetting.ui $$PWD/maskdlggroupsetting.ui $$PWD/maskdlgreagentcode.ui $$PWD/addbatchgroupdialog.ui
RESOURCES += $$PWD/reagents_resources.qrc

# 孔位有效期复用原日历；统一程序与告警页共用，只编译一次。
INCLUDEPATH += $$PWD/../alarm_history/calendar
SOURCES *= $$clean_path($$PWD/../alarm_history/calendar/cuscalendar.cpp)
HEADERS *= $$clean_path($$PWD/../alarm_history/calendar/cuscalendar.h)
FORMS *= $$clean_path($$PWD/../alarm_history/cusmaskcalendar.ui) $$clean_path($$PWD/../alarm_history/calendar/cuscalendar.ui)
RESOURCES *= $$clean_path($$PWD/../quality_control/qc_history_resources.qrc)
# 独立试剂入口也需带齐页面已引用的原库读写能力。
SOURCES *= $$PWD/original_reagent_repository.cpp $$PWD/original_reagent_editor.cpp $$PWD/original_reagent_store.cpp \
           $$clean_path($$PWD/../original_files/original_business_database.cpp)
HEADERS *= $$PWD/original_reagent_repository.h $$PWD/original_reagent_editor.h \
           $$clean_path($$PWD/../original_files/original_business_database.h)
SOURCES *= $$clean_path($$PWD/../original_files/original_ini.cpp)
HEADERS *= $$clean_path($$PWD/../original_files/original_ini.h)
