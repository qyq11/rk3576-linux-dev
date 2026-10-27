QT += widgets sql svg
INCLUDEPATH += $$PWD $$PWD/.. $$PWD/../projects $$PWD/../ui $$PWD/../shell/widgets $$PWD/../shell/widgets/material $$PWD/material
SOURCES += $$PWD/sample_store.cpp $$PWD/sample_page.cpp $$PWD/sample_widgets.cpp $$PWD/cusracklistwidget.cpp $$PWD/cuscheckheader.cpp $$PWD/cuscombox_base.cpp \
    $$PWD/material/qtmaterialradiobutton.cpp $$PWD/material/qtmaterialscrollbar.cpp $$PWD/material/qtmaterialscrollbar_internal.cpp \
    $$PWD/material/lib/qtmaterialcheckable.cpp $$PWD/material/lib/qtmaterialcheckable_internal.cpp
HEADERS += $$PWD/sample_types.h $$PWD/sample_store.h $$PWD/sample_page.h $$PWD/sample_widgets.h $$PWD/cusracklistwidget.h $$PWD/cuscheckheader.h $$PWD/cuscombox_base.h \
    $$PWD/material/qtmaterialradiobutton.h $$PWD/material/qtmaterialradiobutton_p.h $$PWD/material/qtmaterialscrollbar.h $$PWD/material/qtmaterialscrollbar_p.h $$PWD/material/qtmaterialscrollbar_internal.h \
    $$PWD/material/lib/qtmaterialcheckable.h $$PWD/material/lib/qtmaterialcheckable_p.h $$PWD/material/lib/qtmaterialcheckable_internal.h
FORMS += $$PWD/frmsamplematerial.ui $$PWD/frmsamplesettingmaterial.ui $$PWD/cussamplerack.ui $$PWD/maskdlgsampledetail.ui $$PWD/maskdlgrepeatsetting.ui $$PWD/maskdlgcombasetting.ui $$PWD/maskdlgracknosetting.ui
RESOURCES += $$PWD/samples_resources.qrc
SOURCES += $$PWD/sample_task_reset.cpp
HEADERS += $$PWD/sample_task_reset.h
SOURCES *= $$PWD/sample_qc_storage.cpp $$PWD/sample_qc_preset.cpp
include($$PWD/sample_qc_values.pri)

