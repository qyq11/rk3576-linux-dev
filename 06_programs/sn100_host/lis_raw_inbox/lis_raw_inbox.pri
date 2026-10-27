# 独立原报文存储与新增辅助窗口；由主工程显式接入，不启动通信或自动导入。
QT += core sql widgets
CONFIG += c++14
INCLUDEPATH += $$PWD $$PWD/..
SOURCES += $$PWD/lis_dsr_raw.cpp $$PWD/lis_inbox_store.cpp \
           $$PWD/lis_inbox_page.cpp $$PWD/lis_inbox_page_host.cpp $$PWD/lis_inbox_page_worker.cpp
HEADERS += $$PWD/lis_dsr_raw.h $$PWD/lis_inbox_store.h \
           $$PWD/lis_inbox_page.h $$PWD/lis_inbox_page_host.h $$PWD/lis_inbox_page_worker.h
# 会话类型只使用正式 data/account_store.h，不复制声明或在此重复加入其MOC。
