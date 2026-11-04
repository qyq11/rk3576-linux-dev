#ifndef SN100_ORIGINAL_CHANNEL_RECEIVER_H
#define SN100_ORIGINAL_CHANNEL_RECEIVER_H

#include "original_channel_store.h"
#include <QPointer>
#include <QQueue>
#include <functional>
#include <memory>

class DeviceControl;

// GUI适配器：只接被动Uh并把值交专用SQL线程；不发设备命令、不要求人工登录。
class OriginalChannelReceiver : public QObject
{
    Q_OBJECT
public:
    using SnapshotConsumer = std::function<bool(const OriginalRuntime::Channels &, quint64, QString *)>;
    OriginalChannelReceiver(OriginalChannelStore *store, DeviceControl *control, QString originalRoot,
                            SnapshotConsumer consumer, QObject *parent = nullptr);
    ~OriginalChannelReceiver() override;
    bool start();
    bool isStopped() const { return stopped_; }
    int pendingBatchCount() const { return waiting_.size() + (inFlight_ ? 1 : 0); }
    // 仅在worker已释放或专用线程已停止后作异常尾清理；未收到的终态如实标待核。
    void finishAfterWorkerStopped();

public slots:
    void receivePayload(QByteArray payload, quint64 generation);
    void requestStop();

signals:
    void requestOpen(QString originalRoot);
    void requestReload();
    void requestApply(quint64 requestId, quint64 generation, OriginalChannelUpdates updates,
                      QString fingerprint, OriginalRuntime::ChannelCommitPermitPtr permit);
    void requestClose();
    void batchFinished(quint64 requestId, quint64 generation, QString outcome, bool displayed,
                       QString message, int changedRows, int unknownRows);
    // 已提交批次重读恢复的显示结果；不产生第二次提交终态，不重放该批。
    void snapshotRecovered(quint64 requestId, quint64 generation, bool displayed, QString message);
    void error(QString message);
    void stopped();

private:
    struct Batch
    {
        quint64 request = 0, generation = 0;
        qint64 bytes = 0;
        OriginalChannelUpdates updates;
        OriginalRuntime::ChannelCommitPermitPtr permit;
    };
    struct CommittedRecovery
    {
        quint64 request = 0, generation = 0;
        OriginalRuntime::ChannelCommitPermitPtr permit;
    };
    QPointer<OriginalChannelStore> store_;
    QPointer<DeviceControl> control_;
    QString originalRoot_, fingerprint_;
    SnapshotConsumer consumer_;
    QQueue<Batch> waiting_;
    std::unique_ptr<Batch> inFlight_;
    std::unique_ptr<CommittedRecovery> committedRecovery_;
    quint64 nextRequest_ = 0, generation_ = 0;
    qint64 pendingBytes_ = 0;
    bool connected_ = false, started_ = false, ready_ = false, reloadPending_ = false;
    bool stopRequested_ = false, closeRequested_ = false, sourceClosed_ = false, stopped_ = false;
    bool dispatching_ = false, finishing_ = false;

    void synchronizeConnection();
    void dispatch();
    void reloadIfNeeded();
    void cancelWaiting(const QString &reason, const QString &outcome = QStringLiteral("Cancelled"));
    void finishResult(quint64 request, quint64 generation, OriginalRuntime::ChannelApplyResult result);
    void finishStop();
    bool current(const Batch &batch) const;
    bool current(quint64 generation, const OriginalRuntime::ChannelCommitPermitPtr &permit) const;
    void consumeRecoveredSnapshot(const OriginalRuntime::Channels &channels);
    static bool parse(const QByteArray &payload, OriginalChannelUpdates *updates, QString *message);
};

#endif
