#ifndef SN100_ORIGINAL_DEVICE_SYNC_H
#define SN100_ORIGINAL_DEVICE_SYNC_H
#include "original_project_device_repository.h"
#include "projects/original_project_save_notice.h"
#include <QSet>
#include <QElapsedTimer>

struct OriginalConfigurationStep
{
    QList<QByteArray> packets;
    bool complete = false;
    // 无关/重复回包只登记，不重启等待计时、不降级已完成状态、不写库。
    bool ignored = false;
    bool commitSeen = false; // 即使提交后快照/下一同步步失败，也保留实际写入事实。
    QString message;
};

// 一个连接一次同步：先读实际试剂，再逐项目发送Up并以UP回读核对；超时不盲目重发。
class OriginalDeviceSync
{
public:
    bool initialize(const QString &root, QString *message);
    bool begin(quint64 connection, bool linuxNew, OriginalConfigurationStep *step, QString *message);
    bool reagentReplySaved(quint64 connection, OriginalConfigurationStep *step, QString *message);
    bool projectReply(const QByteArray &payload, quint64 connection, OriginalConfigurationStep *step, QString *message);
    bool canStart(bool linuxNew, QString *message) const;
    bool parametersReady(bool linuxNew, QString *message) const;
    void setKnownProjectFiles(const ProjectRecords &projects);
    bool canRecordProjectSave(const QString &fileName, QString *message) const;
    bool reserveProjectSave(quint64 ticket, const QString &fileName, QString *message);
    void releaseProjectSave(quint64 ticket) { reservedProjectSaves_.remove(ticket); }
    OriginalProjectUpdateState noteProjectSave(const OriginalProjectSaveNotice &notice);
    bool prepareProjectUpdate(quint64 connection, QList<QByteArray> *packets, QString *message);
    void projectUpdateSendResult(quint64 intent, quint64 connection, bool attempted, bool queued, const QString &message);
    bool receiveProjectUpdate(const QByteArray &payload, quint64 connection, bool *handled, bool *saved, QString *message);
    void expireProjectUpdate(quint64 intent, quint64 connection);
    OriginalProjectUpdateStates takeProjectUpdateStates();
    quint64 activeProjectUpdateIntent() const { return activeUpdate_ ? activeUpdateState_.intent : 0; }
    bool hasProjectUpdates() const { return activeUpdate_ || !projectUpdates_.isEmpty() || unsupportedUpdate_ || projectIdentityReview_; }
    bool projectUpdateWaiting() const;
    qint64 activeProjectUpdateExpiresAt() const
    { return projectUpdateClock_.isValid() ? projectUpdateClock_.msecsSinceReference()+10000 : 0; }
    bool waiting() const { return waiting_; }
    bool waitingForReagents() const { return waiting_ && waitingForReagents_; }
    quint64 connection() const { return connection_; }
    bool usesSimplifiedProtocol() const { return connection_ && linuxNew_; }
    void invalidate();
private:
    QString root_;
    OriginalProjectDeviceRepository repository_;
    QList<OriginalDeviceProject> projects_;
    int projectIndex_ = 0;
    quint64 connection_ = 0;
    bool linuxNew_ = false, ready_ = false, waiting_ = false, waitingForReagents_ = false;
    bool nextProject(OriginalConfigurationStep *step, QString *message);
    QSet<QString> knownProjectFiles_;
    QMap<quint64,QString> reservedProjectSaves_;
    QMap<QString, OriginalProjectUpdateState> projectUpdates_;
    QMap<QString, QString> updateAnchors_;
    OriginalProjectUpdateState activeUpdateState_, unsupportedUpdateState_;
    OriginalDeviceProject activeUpdateProject_;
    bool activeUpdate_ = false, unsupportedUpdate_ = false;
    bool projectIdentityReview_ = false;
    OriginalProjectUpdateState projectIdentityReviewState_;
    quint64 nextUpdateIntent_ = 0, initialUpdateEpoch_ = 0;
    OriginalProjectUpdateStates updateStates_;
    QElapsedTimer projectUpdateClock_;
    bool activeSendConclusionKnown_ = false, activeSendMayHaveQueued_ = false;
    bool timeoutAwaitingSendConclusion_ = false;
    void recordUpdateState(const OriginalProjectUpdateState &state);
    void reconcileInitialProject(const OriginalDeviceProject &sent, const OriginalDeviceProject &confirmed);
    OriginalProjectUpdateState referenceChangeState(const OriginalProjectUpdateState &source, qint64 oldId, qint64 newId,
        const QString &fingerprint, bool parametersConfirmed);
};
#endif
