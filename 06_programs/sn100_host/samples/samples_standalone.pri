# Standalone/test support; unified shell already owns these implementations.
include($$PWD/../projects/projects_core.pri)
include($$PWD/../projects/projects_widgets.pri)
SOURCES += $$PWD/../ui/cusdialogmask.cpp $$PWD/../ui/cusmessagebox.cpp $$PWD/../shell/widgets/cusbadgebutton.cpp
HEADERS += $$PWD/../ui/cusdialogmask.h $$PWD/../ui/cusmessagebox.h $$PWD/../shell/widgets/cusbadgebutton.h
FORMS += $$PWD/../ui/cusdialogmask.ui $$PWD/../ui/cusmessagebox.ui
RESOURCES += $$PWD/../projects/projects_standalone_resources.qrc

