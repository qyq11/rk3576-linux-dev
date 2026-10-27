QT += core gui widgets sql
INCLUDEPATH += $$PWD $$PWD/.. $$PWD/../shell/widgets $$PWD/../shell/widgets/material
include($$PWD/projects_store_support.pri)
SOURCES += $$PWD/project_page.cpp
HEADERS += $$PWD/project_page.h
FORMS += $$PWD/frmsystemmaterial.ui
RESOURCES += $$PWD/projects_resources.qrc

# Explicit auxiliary configuration operation; ordinary Save and original form stay intact.
SOURCES += $$PWD/project_format_dialog.cpp
HEADERS += $$PWD/project_format_dialog.h
