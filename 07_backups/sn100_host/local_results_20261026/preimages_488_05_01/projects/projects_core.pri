QT += core gui widgets sql
INCLUDEPATH += $$PWD $$PWD/.. $$PWD/../shell/widgets $$PWD/../shell/widgets/material
SOURCES += $$PWD/projects_store.cpp $$PWD/project_page.cpp $$PWD/cuscheckbox.cpp
HEADERS += $$PWD/projects_store.h $$PWD/project_page.h $$PWD/cuscheckbox.h
SOURCES += $$PWD/project_advanced.cpp
HEADERS += $$PWD/project_advanced.h
FORMS += $$PWD/frmsystemmaterial.ui
RESOURCES += $$PWD/projects_resources.qrc
