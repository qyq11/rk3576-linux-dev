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
# Independent O07 offline gates pass before these pages enter the unified shell.
include($$PWD/../projects/projects_core.pri)
include($$PWD/../reagents/reagents.pri)
SOURCES += $$PWD/catalog_shell.cpp
# O08 local preset/task gate: independent offline 14/0 before shell integration.
include($$PWD/../samples/samples_core.pri)
# O09-1 offline file replay only; no sockets, timers/DB or device side effects in the parser.
SOURCES += $$PWD/device_status_shell.cpp $$PWD/../protocol/status_replay.cpp \
           $$PWD/../protocol/status_session.cpp $$PWD/../protocol/frame_codec.cpp
HEADERS += $$PWD/../protocol/status_replay.h $$PWD/../protocol/status_session.h $$PWD/../protocol/frame_codec.h
include($$PWD/../instrument/instrument_widgets.pri)
SOURCES += $$PWD/instrument_shell.cpp
# O02-2独立认证/原登录卡验证通过后接入；原CusMessageBox同步调用保持不变。
SOURCES += $$PWD/login_shell.cpp $$PWD/../ui/maskdlglogin.cpp
HEADERS += $$PWD/../ui/maskdlglogin.h
FORMS += $$PWD/../ui/maskdlglogin.ui
# O09-2只连接本轮自建LocalHost:0模拟端，保持既有只读客户端终态语义。
SOURCES += $$PWD/../protocol/status_loopback.cpp $$PWD/../protocol/read_only_client.cpp \
           $$PWD/../protocol/tcp_transport.cpp $$PWD/../protocol/read_only_messages.cpp
HEADERS += $$PWD/../protocol/status_loopback.h $$PWD/../protocol/read_only_client.h \
           $$PWD/../protocol/tcp_transport.h $$PWD/../protocol/read_only_messages.h
# O10-1独立两QPA各13/0后替换设置子页3；仅本地名称，不执行打印。
include($$PWD/../ui/printer_settings.pri)
SOURCES += $$PWD/printer_shell.cpp
# O10-2独立两QPA各17/0通过后接入本地LIS设置，不加载原活动通信类。
include($$PWD/../ui/lis_settings.pri)
SOURCES += $$PWD/lis_shell.cpp
# O11本地质控；真实测量/临床规则不进入此依赖闭包。
include($$PWD/../quality_control/qc_core.pri)
include($$PWD/../quality_control/qc_widgets.pri)
SOURCES += $$PWD/quality_shell.cpp
include($$PWD/../calibration/calibration.pri)
SOURCES += $$PWD/calibration_shell.cpp
# Local-only final API adapters; no language persistence or device alarm action.
SOURCES += $$PWD/language_shell.cpp $$PWD/alarm_shell.cpp $$PWD/../ui/language_setting_page.cpp
HEADERS += $$PWD/../ui/language_setting_page.h
FORMS += $$PWD/../ui/frmlanguagesetting.ui
include($$PWD/../alarm_history/alarm_history_core.pri)
include($$PWD/../alarm_history/alarm_history_page.pri)
