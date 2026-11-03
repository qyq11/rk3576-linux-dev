#include "frmmain.h"
#include "ui_frmmain.h"
#include "frmuserset.h"
#include "ui/frmaccountsetting.h"
#include "ui/frmhospinfosetting.h"
#include "ui/maskdlglogin.h"
#include "ui/frmprintersetting.h"
#include "ui/frmlissetting.h"
#include "history/history_page.h"
#include "instrument/instrument_page.h"
#include <QCoreApplication>
#include <QTimer>
#include <QScopedValueRollback>

bool frmMain::businessPagesIdle() const
{
    return !hospitalPage()->isBusy() && accountPage() && accountPage()->isReady()
        && !accountPage()->isBusy() && (!history_ || !history_->isBusy())
        && catalogPagesIdle() && qualityPagesIdle() && calibrationPageIdle() && (!instrument_ || !instrument_->isBusy())
        && (!printerPage() || !printerPage()->isBusy())
        && (!lisPage() || !lisPage()->isBusy()) && alarmHistoryIdle();
}

bool frmMain::enablePasswordLogin()
{
    if (!accounts_)
    {
        return false;
    }
    if (!passwordLoginEnabled_)
    {
        passwordLoginEnabled_ = true;
        connect(ui->btnUser, &CusBadgeButton::clicked, this, [this]() { requestLogin(); });
    }
    updateAccountStatus();
    return true;
}

void frmMain::requestStartupLogin()
{
    if (startupLoginConsumed_ || startupLoginPending_) return;
    startupLoginPending_ = true;
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
            this, &frmMain::cancelStartupLogin);
    resumeStartupLogin();
}

void frmMain::cancelStartupLogin()
{
    startupLoginPending_ = false;
    startupLoginQueued_ = false;
    // 本次启动已经结束自动登录意图；用户关闭卡片后只能主动点头像再打开。
    startupLoginConsumed_ = true;
}

void frmMain::resumeStartupLogin()
{
    if (!startupLoginPending_) return;
    if (!passwordLoginEnabled_ || !accounts_ || session_.authenticated || logoutPending_
        || loginAfterLogoutPending_ || shutdownClosing_ || !window()->isVisible()
        || (loginDialog_ && loginDialog_->isVisible()))
    {
        cancelStartupLogin();
        return;
    }
    if (startupLoginQueued_ || !businessPagesIdle() || !shutdownDialog_.isNull()) return;
    startupLoginQueued_ = true;
    QTimer::singleShot(0, this, [this]()
    {
        startupLoginQueued_ = false;
        if (!startupLoginPending_) return;
        if (!passwordLoginEnabled_ || !accounts_ || session_.authenticated || logoutPending_
            || loginAfterLogoutPending_ || shutdownClosing_ || !window()->isVisible()
            || (loginDialog_ && loginDialog_->isVisible()))
        {
            cancelStartupLogin();
            return;
        }
        // 排队以后别的页面可能又开始处理，继续等它现有的忙状态通知。
        if (!businessPagesIdle() || !shutdownDialog_.isNull()) return;
        // 只等待“正在处理”；进入原离页确认前消费，取消草稿确认不会再次询问。
        cancelStartupLogin();
        requestLogin();
    });
}

bool frmMain::rejectLoginNavigation()
{
    if (!loginDialog_ || !loginDialog_->isVisible())
    {
        return false;
    }
    // 包括关闭动画和提示框；先结束当前登录卡，避免父窗先关闭留下悬空弹窗。
    emit navigationRejected(loginDialog_->isBusy()
        ? tr("登录处理中，请等待结果。") : tr("请先关闭登录窗口。"));
    return true;
}

void frmMain::resumeLoginAfterLogout()
{
    if (!loginAfterLogoutPending_ || loginAfterLogoutQueued_ || !passwordLoginEnabled_
        || session_.authenticated || logoutPending_ || !businessPagesIdle())
    {
        return;
    }
    loginAfterLogoutQueued_ = true;
    QTimer::singleShot(0, this, [this]()
    {
        loginAfterLogoutQueued_ = false;
        if (!loginAfterLogoutPending_ || session_.authenticated || shutdownClosing_ || !window()->isVisible())
        {
            loginAfterLogoutPending_ = false;
            return;
        }
        // 注销会让账户页异步重读列表；等回读和成功提示结束后再开登录卡。
        if (logoutPending_ || !businessPagesIdle() || !shutdownDialog_.isNull())
        {
            return;
        }
        loginAfterLogoutPending_ = false;
        requestLogin();
    });
}

bool frmMain::requestLogin()
{
    // 登录卡建立前也可能弹页面草稿确认；未显示卡片时同样保护本次账户切换尝试。
    QScopedValueRollback<bool> interaction(deviceUserInteractionPending_,true);
    // 主动请求登录即结束启动自动请求，忙态或草稿确认拒绝后也不补弹。
    cancelStartupLogin();
    if (!passwordLoginEnabled_ || !accounts_ || logoutPending_)
    {
        return false;
    }
    if (loginDialog_ && loginDialog_->isVisible())
    {
        if (loginDialog_->isBusy())
        {
            emit navigationRejected(tr("登录处理中，请等待结果。"));
            return false;
        }
        loginDialog_->raise();
        loginDialog_->activateWindow();
        return true;
    }
    // 先检查所有隐藏页是否正在提交，再询问草稿；不为登录提前注销旧身份。
    if (!businessPagesIdle())
    {
        emit navigationRejected(tr("数据处理中，请等待完成后再登录。"));
        return false;
    }
    if (!requestAlarmHistoryLeave(tr("登录")) || !requestCalibrationLeave(tr("登录")) || !requestQualityLeave(tr("登录")) || !requestInstrumentLeave(tr("登录")) || !requestCatalogLeave(tr("登录"))
        || !requestHistoryLeave(tr("登录")) || !userSet_->requestLeave(tr("登录")))
    {
        return false;
    }
    auto *dialog = new MaskDlgLogin(accounts_, this);
    dialog->setCompanyName(property("configuredCompanyName").toString());
    loginAfterLogoutPending_ = false;
    loginDialog_ = dialog;
    connect(dialog, &MaskDlgLogin::busyChanged, this, [this](bool) { updateAccountStatus(); });
    connect(dialog, &QDialog::finished, this, [this, dialog]()
    {
        if (loginDialog_ == dialog)
        {
            loginDialog_.clear();
        }
        dialog->deleteLater();
        updateAccountStatus();
    });
    dialog->showDialog();
    updateAccountStatus();
    return true;
}
