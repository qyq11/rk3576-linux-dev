#ifndef SN100_ORIGINAL_CHANNEL_STORE_H
#define SN100_ORIGINAL_CHANNEL_STORE_H

#include "original_files/original_system_profile.h"

#include <QMetaType>
#include <QObject>

using OriginalChannelUpdates = QVector<OriginalRuntime::ChannelSwitchUpdate>;

Q_DECLARE_METATYPE(OriginalRuntime::Channels)
Q_DECLARE_METATYPE(OriginalChannelUpdates)
Q_DECLARE_METATYPE(OriginalRuntime::ChannelCommitPermitPtr)
Q_DECLARE_METATYPE(OriginalRuntime::ChannelApplyResult)

// 只在所属后台线程读写原通道资料；调用方排队关闭后再退出线程。
class OriginalChannelStore : public QObject
{
    Q_OBJECT
public:
    explicit OriginalChannelStore(QObject *parent = nullptr);
    static void registerTypes();

public slots:
    void openOriginal(QString root);
    void reload();
    void apply(quint64 requestId, quint64 generation, OriginalChannelUpdates updates,
               QString expectedFingerprint, OriginalRuntime::ChannelCommitPermitPtr permit);
    void close();

signals:
    void opened(QString root, OriginalRuntime::Channels channels);
    void openFailed(QString message);
    void reloaded(OriginalRuntime::Channels channels, QString error);
    void applied(quint64 requestId, quint64 generation, OriginalRuntime::ChannelApplyResult result);
    void closed();

private:
    bool inWorkerThread(QString *message) const;

    // 首次打开后目录固定；失败可重读，关闭后不可重新打开或写入。
    QString root_;
    bool openRequested_ = false;
    bool ready_ = false;
    bool closed_ = false;
};

#endif
