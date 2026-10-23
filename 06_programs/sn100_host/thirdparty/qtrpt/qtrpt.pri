# QtRPT 3.0.0 source integration for the SN100 Qt5 Widgets report module.
# The original project used NO_BARCODE and left QXLSX_LIBRARY undefined.
QT += core gui widgets xml sql printsupport qml charts
DEFINES += NO_BARCODE
DEFINES -= QXLSX_LIBRARY
include($$PWD/QtRPT/QtRPT.pri)
