#ifndef SN100_SAMPLE_STORE_H
#define SN100_SAMPLE_STORE_H
#include "data/account_store.h"
#include "sample_types.h"
#include "original_task_reagent_receipt.h"
#include <QObject>
#include <QSqlDatabase>
#include <QThread>
#include <QSet>
#include <memory>
#include "projects/projects_store.h"
#include "projects/original_project_save_notice.h"
#include "projects/original_project_save_admission.h"
class OriginalSampleRepository;
class OriginalDetectionSession;
class OriginalDeviceSync;
struct OriginalConfigurationStep;

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
    // 在线程启动前设置；初始化时只调整空孔，已有样本或任务超出新范围则拒绝。
    void configureLayout(int ordinary, int emergency);
    // 主程序在移入工作线程前传入同次已核配置；旧独立入口仍保持原杯默认。
    bool configureOriginalDefaultContainer(bool containerIsTube)
    {
        if (QThread::currentThread() != thread()) return false;
        originalDefaultContainerIsTube_ = containerIsTube;
        return true;
    }
  public slots:
    void initialize(QString catalogDir);
    void initializeOriginal(QString root);
    void loadOriginalQualityChoices(qint64 requestId);
    void saveOriginalCalibrationParameters(qint64 requestId, qint64 projectId, QString expectedVersion, QString lastValues);
    void prepareDeviceTasks(quint64 request, bool linuxNew);
    void cancelDeviceTaskPreparation();
    void queryDeviceRecovery(quint64 request, quint64 connection, QString root);
    void reserveOriginalProjectSave(quint64 ticket, OriginalProjectSaveNotice proposal, qint64 sourceDeadline);
    void releaseOriginalProjectSave(quint64 ticket, QString source);
    void enqueueOriginalProjectUpdate(OriginalProjectSaveNotice notice);
    void advanceOriginalProjectUpdates(quint64 connection);
    void originalProjectUpdateSent(quint64 intent, quint64 connection, bool attempted, bool queued, QString message);
    void answerDeviceRackRequest(QByteArray payload, quint64 connection);
    void receiveDevicePacket(QByteArray payload, quint64 connection, quint64 packet);
    void receiveDevicePacketWithTaskBasis(QByteArray payload, quint64 connection, quint64 packet,
        OriginalTaskReagents::RuntimeBasis basis);
    void beginOriginalDeviceSync(quint64 connection, bool linuxNew);
    void invalidateOriginalDeviceSync();
    void prepareOriginalReagentUpdate(QString holeName, bool updateAmount, quint64 connection);
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
    void originalProfileReady(QString root, int ordinaryHoles, int emergencyHoles);
    void originalProjectsReady(ProjectRecords projects);
    void originalUnitsReady(ProjectUnits units);
    void originalQualityChoicesReady(qint64 requestId, SampleQcPreset::Snapshot choices);
    void originalQualityChoicesError(qint64 requestId, QString message);
    void originalCalibrationParametersSaved(qint64 requestId, bool success, QString message);
    void originalCalibrationRecordsChanged(); // 原校准占号、进度或曲线状态变更；不表示计算完成。
    void deviceTasksPrepared(quint64 request, bool success, QString message);
    void deviceRecoveryState(quint64 request, quint64 connection, QString root,
                             bool known, bool unfinishedTasks, QString message);
    void originalProjectSaveAdmissionReady(OriginalProjectSaveAdmission admission);
    void originalProjectSaveCommitted(OriginalProjectSaveNotice notice, quint64 intent);
    void originalProjectUpdateState(OriginalProjectUpdateState state);
    void originalProjectUpdateBatch(QList<QByteArray> packets, quint64 connection, quint64 intent, qint64 expiresAt);
    void originalProjectUpdatePumpFinished(quint64 connection);
    void deviceRackTasksPrepared(QByteArray payload, quint64 connection);
    void deviceTaskError(QString message);
    void deviceTaskStateChanged();
    void taskReagentCommitObserved(OriginalTaskReagents::Commit receipt); // 已提交或无法确认的原动作事实。
    void originalResultsChanged(); // 原Tr曲线及报告状态保存成功后，历史页重读同一原库。
    void originalReagentsChanged(); // 原Ur写回完成后，试剂页重读，避免只改设备缓存。
    void originalProjectsChanged(); // 原UP保存后，项目页重读同一原库。
    void originalConfigurationPacket(QByteArray payload, quint64 connection);
    void originalConfigurationState(quint64 connection, bool ready, QString message);
    void devicePacketProcessed(quint64 packet, quint64 connection, QByteArray command, bool success, QString message);
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
    std::unique_ptr<OriginalSampleRepository> originalSamples_;
    std::unique_ptr<OriginalDetectionSession> originalDetection_;
    std::unique_ptr<OriginalDeviceSync> originalDeviceSync_;
    QTimer *originalConfigurationTimeout_ = nullptr;
    QTimer *originalProjectUpdateTimeout_ = nullptr;
    QTimer *originalProjectAdmissionTimer_ = nullptr;
    QMap<quint64,std::shared_ptr<OriginalProjectSavePermit>> originalProjectAdmissions_;
    void cleanOriginalProjectAdmissions(bool revokeUnclaimed = false);
    void acceptOriginalProjectAdmission(const OriginalProjectSaveNotice &notice);
    void publishOriginalProjectUpdateStates();
    void sendOriginalConfigurationStep(const OriginalConfigurationStep &step);
    void receiveDevicePacketImpl(QByteArray payload, quint64 connection, quint64 packet,
        const OriginalTaskReagents::RuntimeBasis *taskBasis);
    bool originalRackChange(qint64 request, const QString &operation, const SampleRack &rack, qint64 id);
    QSqlDatabase database_;
    QString connection_;
    AccountSession session_;
    bool ready_ = false;
    bool layoutConfigured_ = false;
    bool originalDefaultContainerIsTube_ = false; // false是原杯默认，不是“未初始化”。
    int ordinaryHoles_ = 8, emergencyHoles_ = 3;
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
