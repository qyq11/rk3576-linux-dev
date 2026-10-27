# 独立入口/界面测试依赖；统一主壳已经拥有这些widgets与resources，不重复包含。
include($$PWD/qc_core.pri)
include($$PWD/qc_widgets.pri)
include($$PWD/../projects/projects_widgets.pri)
SOURCES += $$PWD/../projects/projects_store.cpp $$PWD/../projects/project_advanced.cpp $$PWD/../projects/cuscheckbox.cpp \
           $$PWD/../ui/cusdialogmask.cpp $$PWD/../ui/cusmessagebox.cpp \
           $$PWD/../samples/material/qtmaterialscrollbar.cpp \
           $$PWD/../samples/material/qtmaterialscrollbar_internal.cpp
HEADERS += $$PWD/../projects/projects_store.h $$PWD/../projects/project_advanced.h $$PWD/../projects/cuscheckbox.h \
           $$PWD/../ui/cusdialogmask.h $$PWD/../ui/cusmessagebox.h $$PWD/../samples/material/qtmaterialscrollbar.h \
           $$PWD/../samples/material/qtmaterialscrollbar_p.h $$PWD/../samples/material/qtmaterialscrollbar_internal.h
FORMS += $$PWD/../ui/cusdialogmask.ui $$PWD/../ui/cusmessagebox.ui
RESOURCES += $$PWD/../projects/projects_resources.qrc $$PWD/../shell/account_resources.qrc
