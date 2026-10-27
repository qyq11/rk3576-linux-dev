isEmpty(SN100_PROJECTS_STORE_SUPPORT_INCLUDED) {
    SN100_PROJECTS_STORE_SUPPORT_INCLUDED = 1
    QT += core gui widgets sql
    INCLUDEPATH += $$PWD $$PWD/..
    SOURCES += $$PWD/projects_store.cpp $$PWD/project_advanced.cpp $$PWD/cuscheckbox.cpp
    SOURCES += $$PWD/original_project_repository.cpp
    HEADERS += $$PWD/original_project_repository.h
    HEADERS += $$PWD/projects_store.h $$PWD/project_advanced.h $$PWD/cuscheckbox.h
    SOURCES += $$PWD/projects_current_format.cpp
    HEADERS += $$PWD/project_current_format.h
}
