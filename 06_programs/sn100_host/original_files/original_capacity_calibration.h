#ifndef SN100_ORIGINAL_CAPACITY_CALIBRATION_H
#define SN100_ORIGINAL_CAPACITY_CALIBRATION_H

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QVector>

namespace OriginalRuntime
{
struct CapacityMeasurement
{
    QString area;       // Area_S1或Area_R1。
    QString subArea;    // 保持原SUB_S1/SUB_R1/SUB_R2等名称。
    QString mode;       // FACE/VOL/N1/N2/S1。
    QString bottle;    // 原瓶型，如7mL、15mL。
    QString hole;
    QString markedMicrolitres;
    double averageHeightMillimetres = 0;
};
struct CapacityCalibration
{
    QString root;
    QString relativeFile;
    QString fingerprint;
    QJsonObject original; // 所有未知节点保留，不能重建成只含已知字段的小JSON。
    QVector<CapacityMeasurement> configuredMeasurements() const;
    bool volumeForHeight(const QString &hole, int needle, const QString &area,
                         const QString &bottle, double maximumMicrolitres, int measuredHeightHundredths,
                         double *volumeMicrolitres, QString *message) const;
};
bool loadCapacityCalibration(const QString &root, const QString &relativeFile,
                             CapacityCalibration *result, QString *message);
// 只接受CONFIG中已存在的任务点；FACE/VOL批次完成时按原公式重算BASE并同步针补偿。
// measuredHundredths明确为百分之一毫米，内部转毫米后按原lvCarRetun实际调用链平均；
// 原函数头注释写成乘100，但wgtreagentpanel传的是毫米，不能照注释误放大。
bool saveCapacityMeasurement(const CapacityCalibration &snapshot, const CapacityMeasurement &point,
                             const QVector<int> &measuredHundredths, bool completeMode,
                             QString *message);
}

#endif
