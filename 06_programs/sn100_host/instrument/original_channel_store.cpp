#include "original_channel_store.h"

#include <QCoreApplication>
#include <QDebug>
#include <QThread>

OriginalChannelStore::OriginalChannelStore(QObject *parent) : QObject(parent)
{
    registerTypes();
}

void OriginalChannelStore::registerTypes()
{
    qRegisterMetaType<OriginalRuntime::Channels>("OriginalRuntime::Channels");
    qRegisterMetaType<OriginalChannelUpdates>("OriginalChannelUpdates");
    qRegisterMetaType<OriginalRuntime::ChannelCommitPermitPtr>("OriginalRuntime::ChannelCommitPermitPtr");
    qRegisterMetaType<OriginalRuntime::ChannelApplyResult>("OriginalRuntime::ChannelApplyResult");
}

bool OriginalChannelStore::inWorkerThread(QString *message) const
{
    const auto *application = QCoreApplication::instance();
    if (QThread::currentThread() == thread() && application && thread() != application->thread())
        return true;
    if (message) *message = tr("原通道资料必须在所属后台线程操作，未读写数据库。");
    return false;
}

void OriginalChannelStore::openOriginal(QString root)
{
    QString message;
    if (!inWorkerThread(&message))
    {
        emit openFailed(message);
        return;
    }
    if (closed_ || openRequested_)
    {
        emit openFailed(tr("原通道资料目录已固定或已关闭，不能重新打开；未切换目录。"));
        return;
    }
    openRequested_ = true;
    root_ = root;
    OriginalRuntime::Channels channels;
    if (!OriginalRuntime::readChannelsSnapshot(root_, &channels, &message))
    {
        emit openFailed(message);
        return;
    }
    ready_ = true;
    emit opened(root_, channels);
}

void OriginalChannelStore::reload()
{
    QString message;
    OriginalRuntime::Channels channels;
    if (!inWorkerThread(&message))
    {
        emit reloaded(channels, message);
        return;
    }
    if (closed_ || !openRequested_)
    {
        emit reloaded(channels, tr("原通道资料尚未打开或已关闭，未读取数据库。"));
        return;
    }
    ready_ = OriginalRuntime::readChannelsSnapshot(root_, &channels, &message);
    emit reloaded(ready_ ? channels : OriginalRuntime::Channels(), message);
}

void OriginalChannelStore::apply(quint64 requestId, quint64 generation, OriginalChannelUpdates updates,
    QString expectedFingerprint, OriginalRuntime::ChannelCommitPermitPtr permit)
{
    OriginalRuntime::ChannelApplyResult result;
    if (!inWorkerThread(&result.message))
        result.outcome = OriginalRuntime::ChannelApplyOutcome::Failed;
    else if (closed_)
    {
        result.outcome = OriginalRuntime::ChannelApplyOutcome::Cancelled;
        result.message = tr("原通道资料已关闭，本次通道回读未写入。");
    }
    else if (!permit)
    {
        result.outcome = OriginalRuntime::ChannelApplyOutcome::Failed;
        result.message = tr("缺少通道提交许可，本次通道回读未写入。");
    }
    else if (permit->state() == OriginalRuntime::ChannelCommitPermit::State::Revoked)
    {
        result.outcome = OriginalRuntime::ChannelApplyOutcome::Cancelled;
        result.message = tr("通道提交许可已撤销，本次通道回读未写入。");
    }
    else if (!ready_)
    {
        result.outcome = OriginalRuntime::ChannelApplyOutcome::Failed;
        result.message = tr("原通道资料尚未就绪，请先读取确认；本次通道回读未写入。");
    }
    else
    {
        result = OriginalRuntime::applyTestChannelReadback(root_, updates, expectedFingerprint, permit);
        if (result.outcome == OriginalRuntime::ChannelApplyOutcome::CommittedReloadFailed)
            ready_ = false;
    }
    const char *outcome = "Failed";
    switch (result.outcome)
    {
    case OriginalRuntime::ChannelApplyOutcome::Committed: outcome = "Committed"; break;
    case OriginalRuntime::ChannelApplyOutcome::Cancelled: outcome = "Cancelled"; break;
    case OriginalRuntime::ChannelApplyOutcome::Failed: break;
    case OriginalRuntime::ChannelApplyOutcome::CommittedReloadFailed: outcome = "CommittedReloadFailed"; break;
    }
    qInfo().nospace() << "original-channel apply requestId=" << requestId
                     << " generation=" << generation << " outcome=" << outcome
                     << " changedRows=" << result.changedRows << " unknownRows=" << result.unknownRows;
    // 所有分支汇到同一个回执，调用方据此结束等待并决定是否重读。
    emit applied(requestId, generation, result);
}

void OriginalChannelStore::close()
{
    QString message;
    if (!inWorkerThread(&message))
    {
        qWarning().noquote() << message;
        return;
    }
    ready_ = false;
    closed_ = true;
    emit closed();
}
