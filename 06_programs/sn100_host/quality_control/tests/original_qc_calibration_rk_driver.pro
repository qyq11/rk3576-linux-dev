QT += core sql
QT -= gui
CONFIG += c++14 console warn_on
TEMPLATE = app
TARGET = original_qc_calibration_rk_driver
INCLUDEPATH += ../.. .. ../../calibration
SOURCES += original_qc_calibration_rk_driver.cpp ../qc_store.cpp ../qc_original_store.cpp \
           ../qc_sample_reference_guard.cpp ../qc_history_core.cpp ../../quality/statistics.cpp \
           ../../samples/sample_qc_preset.cpp ../../calibration/calibration_store.cpp \
           ../../calibration/calibration_original_store.cpp
HEADERS += ../qc_store.h ../qc_history_core.h ../qc_history_reply.h ../../quality/statistics.h \
           ../../samples/sample_qc_preset.h ../../samples/sample_qc_protocol.h \
           ../../calibration/calibration_store.h ../../calibration/calibration_types.h \
           ../../calibration/original_sql.h
MOC_DIR = .moc
OBJECTS_DIR = .obj
