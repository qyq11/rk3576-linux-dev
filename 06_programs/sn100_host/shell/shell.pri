INCLUDEPATH += $$PWD $$PWD/.. $$PWD/../ui $$PWD/widgets $$PWD/widgets/material
SOURCES += $$PWD/frmmain.cpp $$PWD/frmuserset.cpp $$PWD/history_shell.cpp $$PWD/shutdown_request.cpp \
           $$PWD/../data/hospital_store.cpp \
           $$PWD/../data/account_store.cpp $$PWD/../ui/frmaccountsetting.cpp \
           $$PWD/../ui/cusmessagebox.cpp $$PWD/../ui/cusdialogmask.cpp \
           $$PWD/../ui/frmhospinfosetting.cpp $$PWD/../ui/mnemonic.cpp \
           $$files($$PWD/widgets/*.cpp) \
           $$files($$PWD/widgets/material/*.cpp) \
           $$files($$PWD/widgets/material/lib/*.cpp)
HEADERS += $$PWD/frmmain.h $$PWD/frmuserset.h $$PWD/shutdown_request.h \
           $$PWD/../data/hospital_store.h \
           $$PWD/../data/account_store.h $$PWD/../ui/frmaccountsetting.h \
           $$PWD/../ui/cusmessagebox.h $$PWD/../ui/cusdialogmask.h \
           $$PWD/../ui/frmhospinfosetting.h $$PWD/../ui/mnemonic.h \
           $$files($$PWD/widgets/*.h) \
           $$files($$PWD/widgets/material/*.h) \
           $$files($$PWD/widgets/material/lib/*.h)
FORMS += $$PWD/frmmain.ui $$PWD/frmuserset.ui \
         $$PWD/widgets/cusflageimage.ui $$PWD/../ui/frmhospinfosetting.ui \
         $$PWD/../ui/frmaccountsetting.ui $$PWD/../ui/cusmessagebox.ui $$PWD/../ui/cusdialogmask.ui
RESOURCES += $$PWD/resources.qrc $$PWD/account_resources.qrc
# 保持独立历史入口不变。主壳qrc已提供同一匿名夹具，避免同名
# resources.qrc生成重复qrc_resources.cpp及初始化符号。
include($$PWD/../history/history_core.pri)
RESOURCES -= $$clean_path($$PWD/../history/resources.qrc)
# The integrated history page owns the original preview/PDF capability too.
include($$PWD/../history/history_report.pri)
