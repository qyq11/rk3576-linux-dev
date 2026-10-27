# Sample/QC共用值及纯校验只链接一次；根负责正式共享构建接线。
isEmpty(SN100_SAMPLE_QC_VALUES_INCLUDED) {
    SN100_SAMPLE_QC_VALUES_INCLUDED = 1
    INCLUDEPATH += $$PWD
    SOURCES *= $$PWD/sample_qc_preset.cpp
    HEADERS *= $$PWD/sample_qc_preset.h $$PWD/sample_qc_protocol.h
}
