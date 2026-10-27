#ifndef SN100_QC_STORE_H
#define SN100_QC_STORE_H

#include <QObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QSqlDatabase>
#include <QVariant>
#include "qc_history_reply.h"
#include "../samples/sample_qc_protocol.h"

struct QcResultRef
{
    int type = 0; // 原结果枚举：原始1、校准2、比值3、INR4；不包含未验临床计算。
    QString name;
    QString unit;
    int decimals = 2;
    double ratio = 1.0;
};
using QcResultRefs = QList<QcResultRef>;
struct QcProjectRef
{
    qint64 id = 0;
    QString name;
    QcResultRefs results;
};
using QcProjectRefs = QList<QcProjectRef>;
struct QcRule
{
    qint64 id = 0;
    qint64 associationId = 0; // quality_rules.item_proj_id，绝非批次ID。
    int resultType = 0;
    QString name;
    bool used = true;
    QString upperLimit;
    QString lowerLimit;
    QString upperBound;
    QString lowerBound;
    QString sdValue;
    QString target;
    QList<int> strategies = QList<int>({0, 0, 0, 0, 0, 0, 0, 0, 0});
};
using QcRules = QList<QcRule>;
struct QcAssociation
{
    qint64 id = 0;
    qint64 controlId = 0;
    qint64 projectId = 0;
    QString projectName;
    QcResultRefs resultRefs; // 关联提交时的单位/倍率/精度快照，不随项目字典变化。
    QcRules rules;
};
using QcAssociations = QList<QcAssociation>;
struct QcBatch
{
    qint64 id = 0;
    qint64 controlId = 0;
    QString name;
    QString expirationDate; // 原串行化yyyy/MM/dd；可保存已过期的历史批次，不推断临床可用性。
    bool current = false;
    bool previous = false;
};
using QcBatches = QList<QcBatch>;
struct QcControl
{
    qint64 id = 0;
    QString name;
    QString manufacturer;
    bool used = true; // 原ckxValid只读回显；保存名称不暗改有效状态。
    QcAssociations projects;
    QcBatches batches;
};
using QcControls = QList<QcControl>;
Q_DECLARE_METATYPE(QcResultRef)
Q_DECLARE_METATYPE(QcResultRefs)
Q_DECLARE_METATYPE(QcProjectRef)
Q_DECLARE_METATYPE(QcProjectRefs)
Q_DECLARE_METATYPE(QcRule)
Q_DECLARE_METATYPE(QcRules)
Q_DECLARE_METATYPE(QcAssociation)
Q_DECLARE_METATYPE(QcAssociations)
Q_DECLARE_METATYPE(QcBatch)
Q_DECLARE_METATYPE(QcBatches)
Q_DECLARE_METATYPE(QcControl)
Q_DECLARE_METATYPE(QcControls)

// 在moveToThread后的所属线程初始化/使用/关闭。本库独立于catalog.db，
// project_id是应用逻辑引用；绝不声称跨文件具有SQLite外键或原子提交。
class QcStore : public QObject
{
    Q_OBJECT
public:
    explicit QcStore(QObject *parent = nullptr);
    ~QcStore() override;
    static void registerTypes();
    static QString validateControl(const QcControl &control);
    static QString validateBatch(const QcBatch &batch);
    static QString validateRules(const QcRules &rules);
public slots:
    void initialize(QString absoluteDirectory);
    void bindCatalog(QString absoluteCatalogDirectory);
    void setCatalog(qint64 requestId, QcProjectRefs projects);
    void load(qint64 requestId);
    void loadHistory(qint64 requestId, qint64 associationId);
    void saveControl(qint64 requestId, QcControl control);
    void removeControl(qint64 requestId, qint64 controlId);
    void setProjects(qint64 requestId, qint64 controlId, QcProjectRefs projects);
    void saveRules(qint64 requestId, qint64 associationId, QcRules rules);
    void saveBatch(qint64 requestId, QcBatch batch);
    void removeBatch(qint64 requestId, qint64 batchId);
    void switchBatch(qint64 requestId, qint64 controlId, qint64 batchId);
    void inspectProjectReference(qint64 requestId, qint64 projectId);
    void loadSampleChoices(qint64 requestId);
    void reserveSampleChange(SampleQcChange change);
    void finishSampleChange(QString changeToken, bool callerCommitted, qint64 observedRevision);
    void reconcileSampleOwner(QString ownerDatasetId, QString catalogCanonicalPath);
    void close();
signals:
    // 本地SQL初始化完成；依赖项目的业务还须等catalogBound对账门槛。
    void ready(QString databasePath);
    void snapshotReady(qint64 requestId, QcControls controls);
    void historyReady(qint64 requestId, QcHistoryReply reply);
    void historyError(qint64 requestId, qint64 associationId, QString code, QString message);
    void catalogReady(qint64 requestId, QcProjectRefs projects);
    void catalogBound(QString databasePath);
    void warning(QString message);
    void succeeded(qint64 requestId, QString operation, qint64 id);
    void error(qint64 requestId, QString operation, QString message);
    void projectReferenceFound(qint64 requestId, qint64 projectId, bool referenced);
    void sampleChoicesReady(qint64 requestId, SampleQcPreset::Snapshot snapshot);
    void sampleChoicesError(qint64 requestId, QString message);
    void sampleChangeReserved(SampleQcReservationReply reply);
    void sampleChangeFinished(SampleQcFinishReply reply);
    void sampleOwnerReconciled(QString ownerDatasetId, QString qualityDatasetId,
                               bool ready, QString message);
    // 无订阅时没有等待；用于诊断两文件提交边界，测试可在真实边界建立确定性屏障。
    void referenceCheckpoint(QString phase, qint64 controlId);
    void closed();
private:
    QSqlDatabase database_;
    QString connectionName_;
    QString datasetId_;
    QSqlDatabase catalogDatabase_;
    QString catalogConnectionName_;
    QcProjectRefs catalog_;
    bool ready_ = false;
    bool referencesReady_ = false;
    bool bindingAttempted_ = false;
    bool check(qint64 requestId, const QString &operation);
    bool checkReferences(qint64 requestId, const QString &operation);
    bool begin(QString *message);
    bool commit(QString *message);
    bool execute(const QString &sql, const QVariantList &values, QString *message, int expected = -1);
    bool controlName(qint64 id, QString *name, QString *message);
    bool historyExists(const QString &where, const QVariantList &values, bool *exists, QString *message);
    bool reserveProjects(qint64 controlId, const QcProjectRefs &projects, QStringList *newKeys, QString *message);
    bool reconcileReferences(QString *message);
    bool validateHistoryReferences(QString *message);
    bool releaseReferences(const QStringList &keys, QString *message);
    bool initializeSampleReferences(QString *message);
    bool beginSamplePair(QString *message);
    bool commitSamplePair(QString *message);
    void rollbackSamplePair();
    bool verifySampleOwner(QString ownerDatasetId, QString catalogCanonicalPath,
                           bool allowBind, QString *message);
    bool readSampleChoicesLocked(SampleQcPreset::Snapshot *snapshot, QString *message);
    bool readSampleIdentitiesLocked(QList<SampleQcIdentity> *identities, QString *message);
    bool sampleHoldMatches(QString ownerDatasetId, const SampleQcIdentity &identity,
                           QString *message);
    bool reconcileSampleReferencesLocked(QString ownerDatasetId, QString *message);
    bool sampleIdentityUnheld(QString kind, qint64 id, QString *message);
    QString referenceKey(qint64 controlId, qint64 projectId) const;
    void fail(qint64 requestId, const QString &operation, const QString &message);
    void release();
};
#endif
