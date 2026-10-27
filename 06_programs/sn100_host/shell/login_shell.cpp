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

bool frmMain::requestLogin()
{
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
