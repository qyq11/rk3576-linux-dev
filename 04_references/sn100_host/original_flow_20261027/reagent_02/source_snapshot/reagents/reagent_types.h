#ifndef SN100_REAGENT_TYPES_H
#define SN100_REAGENT_TYPES_H

#include <QList>
#include <QMetaType>
#include <QString>
#include <QVariantMap>

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
    QVariantMap originalValues; // 原表已读字段；保存其他选项时保留未编辑参数。
    QString originalFingerprint;
};

struct ReagentBottle
{
    qint64 id = 0;
    QString name;
    QString area = QStringLiteral("r_area");
    int capacityUl = 0;
    double heightMm = 0;
    QVariantMap originalValues;
    QString originalFingerprint;
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
    QVariantMap originalValues;
    QString originalFingerprint;
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
    QVariantMap originalValues;
    QString originalFingerprint;
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
    double amountUl = 0;
    double remainingUl = 0; // µL，原used_ul允许小数，不能截成整数。
    int priority = 99;
    QString changedAt;
    QString originalBatch; // 原孔位存试剂批号文字，不是早期catalog批组外键。
    QVariantMap originalValues;
    QString originalFingerprint;
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
    bool originalProfile = false;
    QString originalRoot;
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
