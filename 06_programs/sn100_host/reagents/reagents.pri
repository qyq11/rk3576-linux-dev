QT += widgets sql
INCLUDEPATH += $$PWD $$PWD/.. $$PWD/../projects $$PWD/../ui $$PWD/../shell/widgets $$PWD/../shell/widgets/material
SOURCES += $$PWD/reagent_store.cpp $$PWD/reagents_page.cpp $$PWD/wgtreagentpanel.cpp $$PWD/cusreagenthole.cpp
HEADERS += $$PWD/reagent_types.h $$PWD/reagent_store.h $$PWD/reagents_page.h $$PWD/wgtreagentpanel.h $$PWD/cusreagenthole.h
FORMS += $$PWD/frmreagentmaterial.ui $$PWD/wgtreagentpanel.ui $$PWD/maskdlgreagentlistsetting.ui $$PWD/maskdlgreagentsetting.ui $$PWD/maskdlggroupsetting.ui $$PWD/maskdlgreagentcode.ui $$PWD/addbatchgroupdialog.ui
RESOURCES += $$PWD/reagents_resources.qrc
