# Only standalone/test targets include this. The unified shell owns these already.
SOURCES += $$PWD/../shell/widgets/cusview.cpp \
           $$files($$PWD/../shell/widgets/material/*.cpp) \
           $$files($$PWD/../shell/widgets/material/lib/*.cpp)
HEADERS += $$PWD/../shell/widgets/cusview.h \
           $$files($$PWD/../shell/widgets/material/*.h) \
           $$files($$PWD/../shell/widgets/material/lib/*.h)
