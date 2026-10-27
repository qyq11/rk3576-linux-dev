#ifndef SN100_REAGENT_TYPES_H
#define SN100_REAGENT_TYPES_H

#include <QList>
#include <QMetaType>
#include <QString>

// Stable local keys are separate from instrument names and slot numbers.
struct ReagentRecord
{
    qint64 id = 0;
    QString name;
    QString type = QStringLiteral("试剂");
    QString manufacturer;
    int warningUl = 500;
    int outUl = 100;
    bool valid = true;
    bool checkExpiry = true;
    bool fixed = false;
};

struct ReagentBottle
{
    qint64 id = 0;
    QString name;
    QString area = QStringLiteral("r_area");
    int capacityUl = 0;
    double heightMm = 0;
};

struct ReagentBatch
{
    qint64 id = 0;
    qint64 projectId = 0;
    QString code;
    // Original batch_group is a | separated group of reagent batch numbers.
    QString batchGroup;
    // Original isi_value decimal text; no calculation or calibration approval.
    bool hasMetadata = false;
    QString isiValue;
    QString normalValue;
    // Explicit edit intent keeps old four-field saveBatch callers compatible.
    bool metadataEdited = false;
};

struct ReagentBatchReference
{
    QString ownerModule;
    QString referenceKey;
    qint64 batchId = 0;
};
using ReagentBatchReferences = QList<ReagentBatchReference>;

struct ReagentBarcode
{
    qint64 id = 0;
    qint64 reagentId = 0;
    qint64 bottleId = 0;
    QString code;
    QString expiry;
};

struct ReagentHole
{
    qint64 id = 0;
    QString name;
    QString area;
    qint64 reagentId = 0;
    qint64 bottleId = 0;
    qint64 batchId = 0;
    QString expiry;
    int amountUl = 0;
    int remainingUl = 0;
    int priority = 99;
    QString changedAt;
};

struct ReagentProject
{
    qint64 id = 0;
    QString name;
    QString unit;
    bool enabled = true;
};
using ReagentProjects = QList<ReagentProject>;

struct ReagentSnapshot
{
    QList<ReagentRecord> reagents;
    QList<ReagentBottle> bottles;
    QList<ReagentBatch> batches;
    QList<ReagentBarcode> barcodes;
    QList<ReagentHole> holes;
};

Q_DECLARE_METATYPE(ReagentRecord)
Q_DECLARE_METATYPE(ReagentBottle)
Q_DECLARE_METATYPE(ReagentBatch)
Q_DECLARE_METATYPE(ReagentBatchReference)
Q_DECLARE_METATYPE(ReagentBatchReferences)
Q_DECLARE_METATYPE(ReagentBarcode)
Q_DECLARE_METATYPE(ReagentHole)
Q_DECLARE_METATYPE(ReagentProject)
Q_DECLARE_METATYPE(ReagentProjects)
Q_DECLARE_METATYPE(ReagentSnapshot)
#endif
