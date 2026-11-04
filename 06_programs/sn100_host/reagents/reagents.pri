QT += widgets sql
INCLUDEPATH += $$PWD $$PWD/.. $$PWD/../projects $$PWD/../ui $$PWD/../shell/widgets $$PWD/../shell/widgets/material
SOURCES += $$PWD/reagent_store.cpp $$PWD/reagents_page.cpp $$PWD/wgtreagentpanel.cpp $$PWD/cusreagenthole.cpp
HEADERS += $$PWD/reagent_types.h $$PWD/reagent_store.h $$PWD/reagents_page.h $$PWD/wgtreagentpanel.h $$PWD/cusreagenthole.h
FORMS += $$PWD/frmreagentmaterial.ui $$PWD/wgtreagentpanel.ui $$PWD/wgtreagentpanel_42.ui $$PWD/maskdlgreagentlistsetting.ui $$PWD/maskdlgreagentsetting.ui $$PWD/maskdlggroupsetting.ui $$PWD/maskdlgreagentcode.ui $$PWD/addbatchgroupdialog.ui
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

# 用量窗口只接页面和构建；真实设备收发仍由主窗口另行核对后开放。
SOURCES *= $$PWD/original_reagent_usage.cpp
HEADERS *= $$PWD/original_reagent_usage.h
FORMS *= $$PWD/maskdlgreagentusage.ui
# 独立试剂入口也会显示用量窗口，带齐滚动条；统一程序与样本页只编一次。
SOURCES *= $$clean_path($$PWD/../samples/material/qtmaterialscrollbar.cpp) $$clean_path($$PWD/../samples/material/qtmaterialscrollbar_internal.cpp)
HEADERS *= $$clean_path($$PWD/../samples/material/qtmaterialscrollbar.h) $$clean_path($$PWD/../samples/material/qtmaterialscrollbar_p.h) $$clean_path($$PWD/../samples/material/qtmaterialscrollbar_internal.h)

# 被动N回报使用独立运行来源和窄写；构建本身不开放设备动作。
SOURCES *= $$PWD/original_reagent_level_packet.cpp $$PWD/original_reagent_level_model.cpp $$PWD/original_reagent_level_projects.cpp $$PWD/original_reagent_level_repository.cpp $$PWD/original_reagent_level_runtime.cpp $$PWD/original_reagent_level_worker.cpp $$PWD/reagents_page_level.cpp
HEADERS *= $$PWD/original_reagent_level.h $$PWD/original_reagent_level_runtime.h $$PWD/original_reagent_level_worker.h
SOURCES *= $$clean_path($$PWD/../original_files/original_reagent_capacity.cpp)
HEADERS *= $$clean_path($$PWD/../original_files/original_reagent_capacity.h)
SOURCES *= $$clean_path($$PWD/../original_files/theme_configuration.cpp)
HEADERS *= $$clean_path($$PWD/../original_files/theme_configuration.h)

# 任务剩余量走专用重读和运行采用，不发送设备命令。
SOURCES *= $$PWD/original_task_reagent_display.cpp $$PWD/original_reagent_level_task.cpp $$PWD/original_task_reagent_readback.cpp $$PWD/reagents_page_task.cpp
HEADERS *= $$PWD/original_reagent_page_metadata.h $$PWD/original_reagent_probe.h
HEADERS *= $$PWD/original_task_reagent_display.h $$clean_path($$PWD/../samples/original_task_reagent_receipt.h)
