#include "frmmain.h"
#include "ui/frmlissetting.h"
#include "lis_raw_inbox/lis_inbox_page_host.h"

#include <QAction>
#include <QDebug>
#include <QDir>
#include <QKeySequence>
#include <QTimer>

bool frmMain::enableLisRawInbox(const QString &absoluteDirectory)
{
    if (!lisPage() || lisRawInboxAction_ || !QDir::isAbsolutePath(absoluteDirectory)
        || absoluteDirectory.contains(QChar::Null)) return false;
    lisRawInboxDirectory_ = QDir::cleanPath(absoluteDirectory);
    auto *entry = new QAction(tr("离线原报文收件箱…"), this);
    entry->setObjectName(QStringLiteral("openLisRawInboxAction"));
    entry->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+I")));
    entry->setShortcutContext(Qt::WindowShortcut);
    addAction(entry);
    // 新辅助能力只增加右键菜单/快捷键，不改变原通信设置控件或布局。
    lisPage()->addAction(entry);
    lisPage()->setContextMenuPolicy(Qt::ActionsContextMenu);
    lisRawInboxAction_ = entry;
    connect(entry, &QAction::triggered, this, &frmMain::openLisRawInbox);
    return true;
}

void frmMain::openLisRawInbox()
{
    if (!lisRawInboxAction_ || lisRawInboxShutdownPending_ || lisRawInboxCloseBlocked_
        || rejectLoginNavigation() || logoutPending_) return;
    if (lisRawInbox_)
    {
        lisRawInbox_->showPage();
        return;
    }
    auto *host = LisInboxPageHost::open(this, lisRawInboxDirectory_, session_);
    if (!host)
    {
        emit navigationRejected(tr("无法打开离线原报文收件箱。"));
        return;
    }
    lisRawInbox_ = host; // Host在真实closed/join后自行释放；本窗只保留弱引用。
    connect(this, &frmMain::sessionChanged, host, &LisInboxPageHost::setSession);
    connect(host, &LisInboxPageHost::statusChanged, this,
            [this](const QString &message, bool error)
    {
        lisStatus_ = tr("离线原报文：%1；未映射、未应用检测任务。").arg(message);
        lisStatusError_ = error;
        updateLisStatus();
        qInfo().noquote() << (error ? "LIS_RAW_INBOX_ERROR" : "LIS_RAW_INBOX_STATUS") << message;
    });
    connect(host, &LisInboxPageHost::closed, this, [this, host](bool acknowledged)
    {
        if (lisRawInbox_ != host) return;
        lisRawInbox_ = nullptr;
        qInfo() << "LIS_RAW_INBOX_CLOSED acknowledged=" << acknowledged;
        if (!acknowledged)
        {
            // 缺真实关闭回执不能因Host已销毁就放行下一次退出。
            lisRawInboxCloseBlocked_ = true;
            lisRawInboxShutdownPending_ = false;
            lisRawInboxAction_->setEnabled(false);
            emit navigationRejected(tr("原报文后台已停止但缺数据库关闭回执，未确认正常退出。"));
            return;
        }
        const bool resumeShutdown = lisRawInboxShutdownPending_;
        lisRawInboxShutdownPending_ = false;
        lisRawInboxAction_->setEnabled(!resumeShutdown);
        if (resumeShutdown)
        {
            // 回执后重走全部原草稿守卫；被用户拒绝后不继续自动重试。
            QTimer::singleShot(0, this, [this]()
            {
                if (!window()->close() && lisRawInboxAction_)
                    lisRawInboxAction_->setEnabled(true);
            });
        }
    });
    qInfo() << "LIS_RAW_INBOX_OPENED network=false task_application=false";
}

bool frmMain::requestLisRawInboxShutdown()
{
    if (lisRawInboxCloseBlocked_)
    {
        emit navigationRejected(tr("原报文关闭回执不完整，尚不能确认正常退出。"));
        return false;
    }
    if (!lisRawInbox_) return true;
    lisRawInboxShutdownPending_ = true;
    lisRawInboxAction_->setEnabled(false);
    lisRawInbox_->requestClose();
    return false;
}

bool frmMain::lisRawInboxShutdownComplete() const
{
    return !lisRawInbox_ && !lisRawInboxShutdownPending_ && !lisRawInboxCloseBlocked_;
}
