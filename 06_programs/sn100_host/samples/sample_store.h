#ifndef SN100_SAMPLE_STORE_H
#define SN100_SAMPLE_STORE_H
#include "data/account_store.h"
#include "sample_types.h"
#include <QObject>
#include <QSqlDatabase>
#include <QSet>

class QTimer;

// SQL is opened, used and closed only in this object's worker thread.
class SampleStore : public QObject
{
    Q_OBJECT
  public:
    explicit SampleStore(QObject *parent = nullptr);
    ~SampleStore() override;
    static void registerTypes();
    static bool canEdit(const AccountSession &session);
  public slots:
    void initialize(QString catalogDir);
    void setSession(AccountSession session);
    void load(qint64 requestId);
    void saveRack(qint64 requestId, SampleRack rack);
    void removeRack(qint64 requestId, qint64 rackId);
    void clearOrdinaryRacks(qint64 requestId);
    void saveCombination(qint64 requestId, SampleCombination combination);
    void removeCombination(qint64 requestId, qint64 combinationId);
    void close();
    // 拟议QC公共合同的回执端；仅所属worker线程使用。
    void qualityOwnerReconciled(QString ownerDatasetId, QString qualityDatasetId, bool ready, QString message);
    void qualityChangeReserved(SampleQcReservationReply reply);
    void qualityChangeFinished(SampleQcFinishReply reply);
    void invalidateQualityBinding();
  signals:
    void ready(QString databasePath);
    void snapshotReady(qint64 requestId, SampleSnapshot snapshot);
    void succeeded(qint64 requestId, QString operation, qint64 id);
    void error(qint64 requestId, QString operation, QString message);
    void closed();
    void warning(qint64 requestId, QString operation, QString message);
    void qualityProtectionReady(bool ready);
    void qualityOwnerBindingRequired(QString ownerDatasetId, QString catalogCanonicalPath);
    void reserveQualityChange(SampleQcChange change);
    void finishQualityChange(QString changeToken, bool committed, qint64 observedRevision);

  private:
    bool check(qint64 requestId, const QString &operation, bool write = false);
    bool read(SampleSnapshot *snapshot, QString *message, qint64 onlyRack = 0);
    QString validateRack(const SampleRack &rack);
    bool finish(qint64 requestId, const QString &operation, qint64 id, bool ok, QString message);
    void release();
    QSqlDatabase database_;
    QString connection_;
    AccountSession session_;
    bool ready_ = false;
    struct PendingQualityChange
    {
        qint64 request = 0, targetId = 0, observedRevision = -1;
        QString operation, stage, message;
        SampleRack rack;
        SampleQcChange change;
        bool needsQuality = false, committed = false;
    } pendingQuality_;
    QString qualityOwnerId_, qualityTargetId_;
    QSet<QString> ownQualityChanges_; // 只识别本实例发出的迟到预约；不接管其他写者的pending。
    bool qualityBound_ = false, closeAfterQuality_ = false;
    QTimer *qualityReplyTimer_ = nullptr;
    void setupQualityProtocol();
    bool initializeQualitySchema(QString *message);
    bool readQualityHole(qint64 rackId, SampleHole *hole, QString *message);
    QString qualityHoleProblem(const SampleHole &hole) const;
    bool sameQualityHole(const SampleHole &a, const SampleHole &b) const;
    void startQualityChange(qint64 request, QString operation, SampleRack rack, qint64 targetId);
    bool writeQualityRack(QString *message);
    bool writeQualityHole(qint64 rackId, const SampleHole &hole, QString *message);
    bool eraseQualityRacks(QString *message);
    void commitQualityChange();
    void completeQualityChange(bool reconciled, QString message);
    void qualityReplyTimedOut();
    bool persistPendingQualityChange(QString *message);
    bool readQualityOutcome(QString changeToken, QString *outcome, qint64 *finalRevision, QString *message);
    bool abortQualityOutcome(QString changeToken, QString *outcome, QString *message);
    bool forgetQualityOutcome(QString changeToken, QString *message);
    bool rollbackQualityTransaction(QString *message, bool resolveCurrentWrite = false);
    bool reconcileFailedQualityWrite(QString *message);
    void finishAbortedQualityChange(QString reason);
};
#endif
