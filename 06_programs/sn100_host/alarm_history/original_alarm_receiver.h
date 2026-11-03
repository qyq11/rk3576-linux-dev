#ifndef SN100_ORIGINAL_ALARM_RECEIVER_H
#define SN100_ORIGINAL_ALARM_RECEIVER_H

#include "alarm_history/alarm_history_store.h"
#include "original_files/original_system_profile.h"
#include <QDateTime>
#include <QMap>
#include <QPointer>

namespace AlarmHistory
{
// 只保存失败告警的恢复文件；与Store共用同一工作线程，不读取界面或操作数据库。
class OriginalAlarmRecoveryWriter : public QObject
{
    Q_OBJECT
public:
    explicit OriginalAlarmRecoveryWriter(QString originalRoot);
public slots:
    void save(quint64 recoveryId, quint64 eventRequestId, AlarmHistory::Rows rows,
              bool submittedToStore, QString sourceToken, QString reason);
signals:
    void completed(quint64 recoveryId, QString savedFile, QString error);
private:
    QString originalRoot_;
};

// 在接收线程捕获操作员与时间，再交给专用SQL线程追加原告警库。
// 必须使用独立Store；历史页会自行开关来源，不能共用它的查询Store。
class OriginalAlarmReceiver : public QObject
{
    Q_OBJECT
public:
    OriginalAlarmReceiver(Store *writer, QVector<OriginalRuntime::AlarmRule> rules,
                          QString originalRoot, QObject *parent = nullptr);
    bool openOriginal(QString originalRoot);
    bool isReady() const { return ready_ && !stopRequested_; }
    bool isStopped() const { return stopped_; }
    int pendingBatchCount() const { return pending_.size(); }
    int pendingRecoveryBatchCount() const { return recoveries_.size(); }
    bool hasStorageErrors() const { return storageErrors_; }
    // 异常退出已停事件循环时保存完整未确认批次；文件只留在板端运行资料中。
    // 保存的批次禁止自动重发，须人工核对原库，防止已提交但未回执的记录重复。
    bool savePendingRecovery(QString reason, QString *savedFile, QString *error) const;

public slots:
    // 调用方在实际接收回调内取当前账户名和时间，不能等SQL排队后再取。
    // 与原Windows一致，currentUser是账户名；访客使用原字符串Guest。
    void receivePayload(QByteArray payload, QString currentUser, QDateTime receivedAt);
    // 先停止设备新输入，再调用本入口。等stopped后才能关闭线程或退出事件循环。
    void requestStop();

signals:
    void sourceReadyChanged(bool ready);
    void newEventsReceived(quint64 requestId, AlarmHistory::Rows rows);
    void historyCommitted(quint64 requestId, AlarmHistory::Rows rows);
    // 包含未确认写入的原行，调用方可保留失败证据；失败不会自动重发。
    void historyFailed(quint64 requestId, AlarmHistory::Rows rows, bool submittedToStore, QString message);
    void recoveryFinished(quint64 requestId, QString savedFile, QString error);
    void storageError(QString message);
    void stopped();
    void requestOpen(QString originalRoot);
    void requestAppend(quint64 requestId, QString sourceToken, AlarmHistory::Rows rows);
    void requestSourceClose();
    void requestRecovery(quint64 recoveryId, quint64 eventRequestId, AlarmHistory::Rows rows,
                         bool submittedToStore, QString sourceToken, QString reason);

private:
    struct Pending
    {
        Rows rows;
        qint64 bytes = 0;
        bool submitted = false;
    };
    struct RecoveryPending
    {
        quint64 requestId = 0;
        Pending batch;
        QString sourceToken;
        QString reason;
    };
    QPointer<Store> writer_;
    QPointer<OriginalAlarmRecoveryWriter> recoveryWriter_;
    QVector<OriginalRuntime::AlarmRule> rules_;
    QMap<quint64, Pending> pending_;
    QMap<quint64, RecoveryPending> recoveries_;
    QString expectedPath_;
    QString originalRoot_;
    QString sourceToken_;
    quint64 nextRequest_ = 0;
    quint64 nextRecovery_ = 0;
    qint64 pendingRows_ = 0;
    qint64 pendingBytes_ = 0;
    qint64 recoveryRows_ = 0;
    qint64 recoveryBytes_ = 0;
    bool started_ = false;
    bool opening_ = false;
    bool ready_ = false;
    bool stopRequested_ = false;
    bool closeRequested_ = false;
    bool sourceClosed_ = false;
    bool stopCompletionQueued_ = false;
    bool stopped_ = false;
    bool storageErrors_ = false;

    void submitPending();
    void finishBatch(quint64 requestId, qint64 rows, const QString &error);
    void failAll(const QString &error);
    void reportStorageError(const QString &error);
    void finishStopIfDrained();
    void reportFailedBatch(quint64 requestId, Rows rows, bool submittedToStore, const QString &error);
    void queueRecovery(quint64 requestId, Rows rows, bool submittedToStore, const QString &reason);
};
}

#endif
