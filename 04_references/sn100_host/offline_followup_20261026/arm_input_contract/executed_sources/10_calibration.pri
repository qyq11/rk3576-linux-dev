QT += core gui widgets sql
INCLUDEPATH += $$PWD $$PWD/.. $$PWD/../shell/widgets $$PWD/../shell/widgets/material
SOURCES += $$PWD/calibration_store.cpp $$PWD/calibration_page.cpp $$PWD/calibrationchart.cpp
HEADERS += $$PWD/calibration_types.h $$PWD/calibration_store.h $$PWD/calibration_page.h $$PWD/calibrationchart.h
FORMS += $$PWD/frmcalibrationmaterial.ui
