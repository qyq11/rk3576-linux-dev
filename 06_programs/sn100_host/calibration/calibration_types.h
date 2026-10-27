#ifndef SN100_CALIBRATION_TYPES_H
#define SN100_CALIBRATION_TYPES_H

#include <QList>
#include <QMetaType>
#include <QString>

// Raw, untransformed values. Concentration is stored in concentrationUnit;
// measurements are stored in resultUnit. unitRatio is provenance, never applied
// silently to these values. Mean is a display summary, not a fitted result.
struct CalibrationPoint
{
    int index = 0;
    QString sampleRate;
    double concentration = 0;
    QList<double> measurements;
};

struct CalibrationRecord
{
    qint64 id = 0;
    qint64 revision = 0;
    qint64 projectId = 0;
    qint64 batchId = 0; // 0 = no local reagent batch selected, not Default/apply.
    qint64 sourceId = 0;
    QString projectName;
    QString batchCode;
    QString batchGroup; // Component-group provenance, never an applied-curve binding.
    QString bloodBatch;
    QString createdAt;
    QString changedAt;
    QString method;
    QString xTransform;
    QString yTransform;
    QString concentrationUnit;
    QString resultUnit;
    double unitRatio = 1;
    QList<CalibrationPoint> points;
    bool visible = true;
};

using CalibrationRecords = QList<CalibrationRecord>;
Q_DECLARE_METATYPE(CalibrationPoint)
Q_DECLARE_METATYPE(CalibrationRecord)
Q_DECLARE_METATYPE(CalibrationRecords)

#endif
