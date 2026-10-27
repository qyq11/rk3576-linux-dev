#include "lis_inbox_page_host.h"
#include "lis_inbox_page.h"
#include "lis_inbox_page_worker.h"
#include <QApplication>
#include <QDebug>
#include <QThread>
#include <QTimer>
#include <QUuid>

LisInboxPageHost *LisInboxPageHost::open(QWidget *parent, QString directory, AccountSession session)
{
    if (!qobject_cast<QApplication *>(QCoreApplication::instance())
            || QThread::currentThread() != QCoreApplication::instance()->thread())
    { qWarning() << "LIS raw inbox helper must be opened on the QApplication thread"; return nullptr; }
    return new LisInboxPageHost(parent, directory, session);
}
LisInboxPageHost::LisInboxPageHost(QWidget *parent, QString directory, AccountSession session)
    : QObject(nullptr), thread_(new QThread(this)), worker_(new LisInboxPageWorker),
      page_(new LisInboxPage(parent)), session_(session), directory_(directory)
{
    LisInboxStore::registerTypes();
    thread_->setObjectName(QStringLiteral("LisRawInboxAuxiliary"));
    worker_->moveToThread(thread_); // Its unchanged Store child moves with it, before SQL initialize.
    connect(this, &LisInboxPageHost::initializeWorker, worker_, &LisInboxPageWorker::initialize, Qt::QueuedConnection);
    connect(this, &LisInboxPageHost::sessionToWorker, worker_, &LisInboxPageWorker::setSession, Qt::QueuedConnection);
    connect(this, &LisInboxPageHost::importToWorker, worker_, &LisInboxPageWorker::importFile, Qt::QueuedConnection);
    connect(this, &LisInboxPageHost::discardToWorker, worker_, &LisInboxPageWorker::discardDraft, Qt::QueuedConnection);
    connect(this, &LisInboxPageHost::queryWorker, worker_, &LisInboxPageWorker::query, Qt::QueuedConnection);
    connect(this, &LisInboxPageHost::detailWorker, worker_, &LisInboxPageWorker::detail, Qt::QueuedConnection);
    connect(this, &LisInboxPageHost::closeWorker, worker_, &LisInboxPageWorker::close, Qt::QueuedConnection);
    connect(thread_, &QThread::started, this, [this]() { if (!closing_) emit initializeWorker(directory_, session_); });
    connect(worker_, &LisInboxPageWorker::ready, this, [this](const QString &)
    {
        if (pending_ != Pending::Initialize) return;
        ready_ = true; pending_ = Pending::None; updateView();
        if (!closing_) { report(tr("收件箱已收到就绪回执，正在查询真实列表。")); loadPage(0); }
    }, Qt::QueuedConnection);
    connect(worker_, &LisInboxPageWorker::ingested, this, [this](qint64 request, LisDsrRaw::Message actual, bool duplicate)
    {
        if (pending_ != Pending::Import || request != activeRequest_) return;
        pending_ = Pending::None; committed_ = true;
        if (page_)
        {
            page_->setDraft(draftPath_, identity_, expectedControl_, committed_);
            page_->showReceipt(actual.id, duplicate); page_->showDetail(actual);
        }
        updateView();
        report(tr("真实提交回执：收件ID %1；仍为原始报文，未映射、未应用任务。").arg(actual.id));
        if (!closing_) loadPage(0); // Separate UI refresh, not a claimed second transactional verification.
    }, Qt::QueuedConnection);
    connect(worker_, &LisInboxPageWorker::listReady, this, [this](qint64 request, LisDsrRaw::Summaries rows, qint64 total)
    {
        if (pending_ != Pending::List || request != activeRequest_) return;
        pending_ = Pending::None;
        if (page_) page_->showList(rows, total, requestedOffset_);
        updateView(); report(tr("已收到真实列表：本页%1条，共%2条。").arg(rows.size()).arg(total));
    }, Qt::QueuedConnection);
    connect(worker_, &LisInboxPageWorker::detailReady, this, [this](qint64 request, LisDsrRaw::Message actual)
    {
        if (pending_ != Pending::Detail || request != activeRequest_) return;
        pending_ = Pending::None;
        if (page_) page_->showDetail(actual);
        updateView(); report(tr("已读取实际收件ID %1的完整原始详情。").arg(actual.id));
    }, Qt::QueuedConnection);
    connect(worker_, &LisInboxPageWorker::draftDiscarded, this, [this](qint64 request)
    {
        if (pending_ != Pending::Discard || request != activeRequest_) return;
        pending_ = Pending::None;
        draftPath_ = replacementPath_; replacementPath_.clear();
        identity_.clear(); expectedControl_.clear(); committed_ = false;
        if (page_) page_->setDraft(draftPath_, identity_, expectedControl_, committed_);
        updateView(); report(draftPath_.isEmpty() ? tr("草稿已取消；已保存的收件记录保留。")
                                                   : tr("已选择新的文件草稿，尚未读取、尚未保存。确认导入时才建立本地接收标识。"));
    }, Qt::QueuedConnection);
    connect(worker_, &LisInboxPageWorker::error, this, &LisInboxPageHost::handleError, Qt::QueuedConnection);
    connect(worker_, &LisInboxPageWorker::closed, this, [this]() { storeClosed_ = true; ready_ = false; }, Qt::QueuedConnection);
    // quit is thread-safe; this direct connection also allows the worker to stop
    // independently of the GUI callback after its real Store close receipt.
    connect(worker_, &LisInboxPageWorker::closed, thread_, &QThread::quit, Qt::DirectConnection);
    connect(thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(thread_, &QThread::finished, this, &LisInboxPageHost::finishThreadStop);
    connect(page_, &LisInboxPage::fileSelected, this, &LisInboxPageHost::replaceDraft);
    connect(page_, &LisInboxPage::importRequested, this, &LisInboxPageHost::importDraft);
    connect(page_, &LisInboxPage::discardRequested, this, [this]() { replaceDraft(QString()); });
    connect(page_, &LisInboxPage::listRequested, this, &LisInboxPageHost::loadPage);
    connect(page_, &LisInboxPage::detailRequested, this, &LisInboxPageHost::loadDetail);
    connect(page_, &LisInboxPage::reopenRequested, this, &LisInboxPageHost::reopen);
    connect(page_, &LisInboxPage::closeRequested, this, &LisInboxPageHost::requestClose);
    connect(page_, &QObject::destroyed, this, [this]() { page_ = nullptr; requestClose(); });
    updateView(); page_->show(); thread_->start();
}
LisInboxPageHost::~LisInboxPageHost()
{
    Q_ASSERT(!thread_->isRunning()); // Self-deletion is scheduled only by finished.
}
void LisInboxPageHost::finishThreadStop()
{
    // finished can precede final thread-local destruction. wait(0) never blocks
    // the GUI; one timer retries until the thread is actually joined. Never
    // destroy a running QThread or manufacture a successful close on timeout.
    if (!thread_->wait(0)) { QTimer::singleShot(10, this, &LisInboxPageHost::finishThreadStop); return; }
    worker_ = nullptr;
    if (!storeClosed_) report(tr("后台已停止，但未收到收件箱正常关闭回执；不能声称正常关闭。"), true);
    if (page_) page_->finishClose();
    emit closed(storeClosed_);
    deleteLater();
}
bool LisInboxPageHost::onGui()
{
    if (QThread::currentThread() == thread()) return true;
    emit statusChanged(QStringLiteral("辅助Host只能通过GUI线程或排队调用操作。"), true);
    return false;
}
bool LisInboxPageHost::mayImport() const
{
    return session_.authenticated && session_.role >= 1 && session_.role <= 5
        && session_.source != AccountSessionSource::Guest;
}
bool LisInboxPageHost::acceptRequest(bool needsReady)
{
    return onGui() && !closing_ && worker_ && pending_ == Pending::None && (!needsReady || ready_);
}
qint64 LisInboxPageHost::begin(Pending operation)
{
    pending_ = operation; activeRequest_ = ++serial_; updateView(); return activeRequest_;
}
void LisInboxPageHost::updateView()
{
    if (page_) page_->setState(ready_, mayImport(), pending_ != Pending::None, closing_);
}
void LisInboxPageHost::report(QString message, bool error)
{
    if (page_) page_->showStatus(message, error);
    emit statusChanged(message, error);
}
void LisInboxPageHost::setSession(AccountSession session)
{
    if (!onGui() || closing_) return;
    session_ = session; emit sessionToWorker(session_); updateView();
    // Accepted operations already ahead of this session event may finish. This
    // disables new imports immediately; it does not claim to undo a commit.
    if (!mayImport()) report(tr("当前会话只读；不能发起新导入。已排队操作仍以其实际回执为准。"));
}
void LisInboxPageHost::replaceDraft(QString path)
{
    if (!acceptRequest(false)) return;
    if (path == draftPath_) return;
    replacementPath_ = path;
    emit discardToWorker(begin(Pending::Discard));
}
void LisInboxPageHost::importDraft(QString expected)
{
    if (!acceptRequest() || !mayImport() || draftPath_.isEmpty() || committed_) return;
    if (expected.isEmpty()) { report(tr("请原样填写预期报文控制号；本窗口不生成网络控制号。"), true); return; }
    if (identity_.isEmpty())
    {
        identity_ = QUuid::createUuid().toString(QUuid::WithoutBraces); expectedControl_ = expected;
    }
    else if (expected != expectedControl_)
    { report(tr("重试不能改变控制号。请显式取消草稿后重新选择。"), true); return; }
    if (page_) page_->setDraft(draftPath_, identity_, expectedControl_, committed_);
    const qint64 request = begin(Pending::Import);
    report(tr("正在后台读取和提交；尚未收到保存成功回执。"));
    emit importToWorker(request, draftPath_, identity_, expectedControl_);
}
void LisInboxPageHost::loadPage(int offset)
{
    if (!acceptRequest() || offset < 0 || offset > LisDsrRaw::MaximumMessages) return;
    requestedOffset_ = offset;
    const qint64 request = begin(Pending::List);
    report(tr("正在读取收件箱真实列表。")); emit queryWorker(request, offset);
}
void LisInboxPageHost::loadDetail(qint64 id)
{
    if (!acceptRequest() || id <= 0) return;
    const qint64 request = begin(Pending::Detail);
    report(tr("正在读取实际收件ID %1；详情区暂保留上次成功内容，其ID显示在详情上方。").arg(id));
    emit detailWorker(request, id);
}
void LisInboxPageHost::reopen()
{
    if (!acceptRequest(false)) return;
    ready_ = false; begin(Pending::Initialize);
    report(tr("正在重新打开并校验同一收件箱；不清空数据和重试身份。"));
    emit initializeWorker(directory_, session_);
}
void LisInboxPageHost::handleError(qint64 request, QString operation, QString message)
{
    if ((request == 0 && pending_ == Pending::Initialize)
            || (request == activeRequest_ && pending_ != Pending::None))
    {
        const Pending failed = pending_; pending_ = Pending::None;
        if (failed == Pending::Initialize) ready_ = false;
        updateView();
        const QString context = failed == Pending::Import
            ? tr("未收到提交成功回执；保留同一文件、控制号、接收标识和已捕获字节供重试。")
            : failed == Pending::List || failed == Pending::Detail ? tr("查询未成功；屏幕保留上次成功读取的内容。") : QString();
        report(operation + QStringLiteral("：") + message + QChar('\n') + context, true);
    }
    else emit statusChanged(tr("未匹配的后台错误回执：%1：%2").arg(operation, message), true);
}
void LisInboxPageHost::requestClose()
{
    if (!onGui() || closing_ || !worker_) return;
    closing_ = true; updateView();
    report(tr("正在等待后台当前操作和真实关闭回执；不会中断或假称撤销提交。"));
    emit closeWorker(); // FIFO after accepted work; no GUI wait/SQL/terminate.
}
