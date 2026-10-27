#include "frmmain.h"
#include "ui_frmmain.h"
#include "frmuserset.h"
#include "ui/frmhospinfosetting.h"
#include "ui/frmaccountsetting.h"
#include "ui/maskdlglogin.h"
#include "ui/frmlissetting.h"
#include "history/history_page.h"
#include "history/report_controller.h"
#include "instrument/instrument_page.h"

#include <QButtonGroup>
#include <QCloseEvent>
#include <QDateTime>
#include <QFontMetrics>
#include <QLabel>
#include <QListWidget>
#include <QTimer>
#include <QVBoxLayout>

frmMain::frmMain(HospitalStore *store, QWidget *parent, AccountStore *accounts) :
    QWidget(parent),
    ui(new Ui::frmMain),
    userSet_(nullptr),
    menuButtons_(new QButtonGroup(this)),
    accounts_(accounts)
{
    ui->setupUi(this);
    setWindowTitle(accounts ? QStringLiteral("AutoCoagulationAnalyzer — O03-2 脱机")
                           : QStringLiteral("AutoCoagulationAnalyzer — O03-1 脱机"));
    // 保留原逻辑画布；较小屏幕由入口的滚动宿主提供完整访问，不缩放重画。
    setFixedSize(1920,1080);

    const QStringList menuNames = {
        tr("试剂设置"), tr("样本设置"), tr("结果报告"), tr("校准曲线"),
        tr("质量控制"), tr("分析项目"), tr("用户设置"), tr("仪器操作")
    };
    const QStringList menuImages = {
        QStringLiteral("menu_reagent"), QStringLiteral("menu_sample"),
        QStringLiteral("menu_report"), QStringLiteral("menu_calibration"),
        QStringLiteral("menu_quality"), QStringLiteral("menu_project"),
        QStringLiteral("menu_user"), QStringLiteral("menu_driver")
    };
    const QStringList objectNames = {
        QStringLiteral("btnReagentAdptive"), QStringLiteral("btnSampleAdptive"),
        QStringLiteral("btnReportAdptive"), QStringLiteral("btnCalibrateAdptive"),
        QStringLiteral("btnQualityAdptive"), QStringLiteral("btnSystemAdptive"),
        QStringLiteral("btnUserSettingAdptive"), QStringLiteral("btnDivStatusAdptive")
    };
    menuButtons_->setExclusive(true);
    for (int index = 0; index < menuNames.size(); ++index)
    {
        auto *button = new CusMenuButton(ui->lstMenuButton);
        button->setObjectName(objectNames.at(index));
        button->setButtonIco(QStringLiteral(":/shell/main_btn/%1.png").arg(menuImages.at(index)));
        button->setButtonInfo(menuNames.at(index));
        button->setAccessibleName(menuNames.at(index));
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setFocusPolicy(Qt::NoFocus);
        menuButtons_->addButton(button,index);
        auto *item = new QListWidgetItem;
        item->setSizeHint(QSize(165,80));
        ui->lstMenuButton->addItem(item);
        ui->lstMenuButton->setItemWidget(item,button);
        connect(button, &QPushButton::clicked, this, [this, index]()
        {
            selectMainPage(index);
        });

        if (index == 6)
        {
            userSet_ = new frmUserSet(store,ui->stkCenterAdptive,accounts);
            ui->stkCenterAdptive->addWidget(userSet_);
        }
        else
        {
            auto *page = new QWidget(ui->stkCenterAdptive);
            page->setObjectName(QStringLiteral("offlineMainPage%1").arg(index));
            page->setProperty("offlineUnavailable",true);
            auto *layout = new QVBoxLayout(page);
            auto *label = new QLabel(tr("%1：尚未接入（脱机）").arg(menuNames.at(index)),page);
            label->setObjectName(QStringLiteral("offlinePlaceholderLabel"));
            label->setTextFormat(Qt::PlainText);
            label->setAlignment(Qt::AlignCenter);
            label->setEnabled(false);
            layout->addWidget(label);
            ui->stkCenterAdptive->addWidget(page);
        }
    }
    ui->stkCenterAdptive->setCurrentIndex(6);
    restoreMainSelection();

    ui->wgtLogoBg->setBackgroundColor(QColor(28,28,28));
    ui->statusArea->setBackgroundColor(Qt::white);
    ui->menuArea->setBackgroundColor(QColor(28,28,28));
    QLinearGradient gradient(0,0,0,ui->menuArea->height());
    gradient.setColorAt(0,QColor(255,255,255,0));
    gradient.setColorAt(1,QColor(255,255,255,25));
    gradient.setSpread(QGradient::PadSpread);
    ui->menuArea->setGraBackgroundColor(gradient);
    ui->menuArea->setShadowEffect(Qt::AlignRight);
    // 原动态 logoSize=82×82 的居中公式，而不是 .ui 编辑时的暂存坐标。
    ui->label_2->setGeometry(43,18,82,82);
    ui->label_2->setStyleSheet(QStringLiteral(
        "QLabel { border-image: url(:/shell/logo/sn_logo_2.png); }"));
    configureOfflineStatus();

    if (accounts)
    {
        // Identity comes only from the account worker. A successful update can
        // clear it while the account page is still finishing its reload/dialog;
        // do not run the manual logout guard or discard hospital drafts then.
        connect(accounts, &AccountStore::sessionChanged,
                this, &frmMain::applyAccountSession);
        connect(accounts, &AccountStore::error, this,
                [this](const QString &operation, const QString &)
        {
            if (operation == QStringLiteral("session") && logoutPending_)
            {
                logoutPending_ = false;
                updateAccountStatus();
            }
        });
        connect(accountPage(), &frmAccountSetting::busyChanged,
                this, [this](bool) { updateAccountStatus(); });
        connect(ui->btnLoginOut, &QPushButton::clicked,
                this, [this]() { requestLogout(); });
    }

    connect(userSet_, &frmUserSet::pageChanged, this, [this](int userIndex)
    {
        if (currentMainPage() == 6) updateHospitalStatus();
        emit navigationChanged(currentMainPage(),userIndex);
    });
    connect(userSet_, &frmUserSet::navigationRejected, this, &frmMain::navigationRejected);
    connect(hospitalPage(), &frmHospInfoSetting::statusChanged, this,
        [this](const QString &text, bool error)
    {
        hospitalStatus_ = text;
        hospitalStatusError_ = error;
        if ((currentMainPage() == 0 && reagents_) || (currentMainPage() == 1 && samples_)
            || (currentMainPage() == 5 && projects_)) updateCatalogStatus();
        else if (currentMainPage() == 2) updateHistoryStatus();
        else updateHospitalStatus();
        updateAccountStatus();
    });
    // Hospital setBusy changes precede operationFinished; refresh controls when
    // that final event arrives as well as on its ordinary status notifications.
    connect(hospitalPage(), &frmHospInfoSetting::operationFinished,
            this, [this](const QString &, bool) { updateAccountStatus(); });
    // SQL stays in the worker. Only successfully loaded/committed values reach
    // report configuration; edits in ldtHospName are never read for printing.
    connect(store, &HospitalStore::loaded, this,
            [this](const QString &, const QString &name, HospitalRows) { updateReportHospitalName(name); });
    connect(store, &HospitalStore::saved, this,
            [this](const QString &, const QString &name, HospitalRows) { updateReportHospitalName(name); });
    connect(store, &HospitalStore::allLoaded, this,
            [this](const QString &name, HospitalRows) { updateReportHospitalName(name); });
    updateAccountStatus();
}

frmMain::~frmMain()
{
    // Also covers startup failure or an external app.exit while a render runs.
    // Reap the helper before destroying the pages and their status receivers.
    delete reports_;
    reports_ = nullptr;
    delete ui;
}

void frmMain::updateHospitalStatus()
{
    if (calibration_ && currentMainPage() == 3)
    {
        updateCalibrationStatus();
        return;
    }
    if (qualityControl_ && currentMainPage() == 4)
    {
        updateQualityStatus();
        return;
    }
    if (lisPage() && currentMainPage() == 6 && currentUserPage() == 4)
    {
        updateLisStatus();
        return;
    }
    if (printerPage() && currentMainPage() == 6 && currentUserPage() == 3)
    {
        updatePrinterStatus();
        return;
    }
    if (instrument_ && currentMainPage() == 7)
    {
        updateInstrumentStatus();
        return;
    }
    if (readOnlyStatusEnabled_ && currentMainPage() == 7)
    {
        updateReadOnlyStatusDisplay();
        return;
    }
    // 沿用原81px状态区，换页时恢复对应业务的最后结果。
    const QString state = hospitalStatusError_ ? tr("脱机／失败")
        : hospitalPage()->isBusy() ? tr("脱机／处理中")
        : hospitalPage()->hasUnsavedChanges() ? tr("脱机／未保存") : tr("脱机／就绪");
    ui->lblDivState->setText(ui->lblDivState->fontMetrics().elidedText(
        state,Qt::ElideRight,ui->lblDivState->width()));
    ui->lblDivState->setStyleSheet(hospitalStatusError_
        ? QStringLiteral("QLabel { color: #E83323; }")
        : QStringLiteral("QLabel { color: rgb(170,170,170); }"));
    ui->lblDivState->setToolTip(hospitalStatus_);
    ui->statusArea->setToolTip(tr("脱机；仪器状态未知。医院信息：%1").arg(hospitalStatus_));
}

void frmMain::configureOfflineStatus()
{
    ui->lblDivType->setText(QStringLiteral("SN100"));
    ui->lblDivState->setText(tr("脱机／未知"));
    ui->lblUserName->setText(tr("您好,Guest"));
    ui->lblUserName->setToolTip(tr("脱机主框架，尚未接入账户会话。"));
    ui->wgtDivStateLamp->setFlashImage(QStringLiteral(":/shell/main_state/status_off.png"));
    ui->wgtDivStateLamp->setEnabled(false);

    const QList<QPair<CusBadgeButton *, QString>> badges = {
        {ui->btnAbnormal, QStringLiteral("main_state/alarm")},
        {ui->btnShutDown, QStringLiteral("main_btn/shutdown")},
        {ui->btnCloseBeep, QStringLiteral("main_state/closebeep")},
        {ui->btnLis, QStringLiteral("main_state/lisoff")}
    };
    for (const auto &badge : badges)
    {
        badge.first->setNormalBgImage(QStringLiteral(":/shell/%1_normal.png").arg(badge.second));
        badge.first->setHoverBgImage(QStringLiteral(":/shell/%1_hover.png").arg(badge.second));
        badge.first->setPressedBgImage(QStringLiteral(":/shell/%1_pressed.png").arg(badge.second));
        badge.first->setEnabled(false);
        badge.first->setToolTip(tr("脱机：未接入设备，状态未知，操作不可用。"));
    }
    ui->btnUser->setNormalBgImage(QStringLiteral(":/shell/main_state/user_pic.png"));
    ui->btnUser->setHoverBgImage(QStringLiteral(":/shell/main_state/user_pic.png"));
    ui->btnUser->setPressedBgImage(QStringLiteral(":/shell/main_state/user_pic.png"));
    ui->btnUser->setEnabled(false);
    ui->btnUser->setToolTip(accounts_
        ? tr("脱机：只使用显式匿名模拟会话；正式密码登录尚未接入。")
        : tr("账户尚未接入（脱机）。"));
    ui->btnLis->setPos(16,16);
    ui->btnLis->setSizeText(20);
    ui->btnLis->setText(QStringLiteral("off"));
    ui->btnLoginOut->setBackgroundMode(Qt::TransparentMode);
    ui->btnLoginOut->setForegroundColor(QColor(QStringLiteral("#DB4437")));
    ui->btnLoginOut->setEnabled(false);
    ui->btnLoginOut->setToolTip(tr("账户会话尚未接入（脱机）。"));

    const QList<QPair<CusMenuButton *, QString>> deviceButtons = {
        {ui->btnStart,QStringLiteral("start")}, {ui->btnPause,QStringLiteral("pause")}
    };
    for (const auto &device : deviceButtons)
    {
        device.first->setButtonIco(QStringLiteral(":/shell/main_btn/%1.png").arg(device.second));
        device.first->setDisableIco(QStringLiteral(":/shell/main_btn/%1_disable.png").arg(device.second));
        device.first->setButtonStartInfo(device.first == ui->btnStart ? tr("开始") : tr("暂停"));
        device.first->setStyleSheet(QStringLiteral(
            "QPushButton { border: none; background-color: rgba(232,51,35,200); color: white; }"
            "QPushButton:hover { background-color: rgb(242,89,76); color: white; }"
            "QPushButton:pressed { background-color: rgb(211,47,47); color: white; }"
            "QPushButton:disabled { background-color: transparent; color: #636363; }"));
        device.first->setBtnDisabled(true);
        device.first->setToolTip(tr("脱机：设备操作不可用。"));
    }
    const QList<QLabel *> stateIcons = {
        ui->lblCupHave,ui->lblWasteCup,ui->lblTemp,ui->lblWaterHave,ui->lblWasteWater
    };
    for (QLabel *icon : stateIcons)
    {
        icon->setEnabled(false);
        icon->setToolTip(tr("脱机：此设备状态未知。"));
    }
    const auto updateLocalClock = [this]()
    {
        ui->lblSystemTime->setText(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss\nyyyy-MM-dd")));
    };
    updateLocalClock();
    auto *clock = new QTimer(this);
    clock->setInterval(1000);
    connect(clock,&QTimer::timeout,this,updateLocalClock);
    clock->start();
    ui->lblSystemTime->setToolTip(tr("本机时间；未向仪器校时。"));
}

frmHospInfoSetting *frmMain::hospitalPage() const
{
    return userSet_->hospitalPage();
}

frmAccountSetting *frmMain::accountPage() const
{
    return userSet_->accountPage();
}

void frmMain::updateAccountStatus()
{
    const bool loginOpen = loginDialog_ && loginDialog_->isVisible();
    userSet_->setNavigationLocked(logoutPending_ || loginOpen);
    if (!accounts_)
    {
        return;
    }
    // The original user-name region is narrow. Preserve its geometry and show
    // the complete anonymous actor/role plus simulation boundary in a tooltip.
    const QString name = session_.authenticated ? session_.name : QStringLiteral("Guest");
    const QString role = session_.role == 0 ? tr("访客") : AccountStore::roleName(session_.role);
    const QString identity = QStringLiteral("%1: %2").arg(role,name);
    ui->lblUserName->setText(ui->lblUserName->fontMetrics().elidedText(
        identity,Qt::ElideRight,ui->lblUserName->width()));
    const QString source = session_.source == AccountSessionSource::Password
        ? tr("本地测试库密码认证") : session_.source == AccountSessionSource::Simulated
        ? tr("显式匿名模拟会话") : tr("Guest访客会话");
    ui->lblUserName->setToolTip(tr("%1；%2。设备动作状态为模拟%3，登录不改变设备动作条件。")
        .arg(identity,source,session_.actionReady ? tr("ready") : tr("running")));
    const bool idle = businessPagesIdle() && !logoutPending_ && !loginOpen;
    ui->btnUser->setEnabled(passwordLoginEnabled_ && idle);
    ui->btnUser->setToolTip(passwordLoginEnabled_
        ? tr("本地密码登录；只验证独立测试库账户，不连接仪器。")
        : tr("脱机：只使用显式匿名模拟会话；密码登录未启用。"));
    ui->btnLoginOut->setEnabled(session_.authenticated && idle);
    ui->btnLoginOut->setToolTip(logoutPending_ ? tr("正在清除本地会话…")
        : tr("注销本地会话；数据处理中不可用，未保存更改需先确认。"));
}

void frmMain::applyAccountSession(const AccountSession &session)
{
    const bool wasAuthenticated = session_.authenticated;
    session_ = session;
    logoutPending_ = false;
    updateAccountStatus();
    emit sessionChanged(session_);
    if (wasAuthenticated && !session_.authenticated)
    {
        // Store::sessionChanged is emitted after clearing its actual identity.
        // The page receives that same event first and clears its edit caches.
        emit loggedOut();
    }
}

bool frmMain::requestLogout()
{
    if (rejectLoginNavigation())
    {
        return false;
    }
    if (!accounts_ || !session_.authenticated || logoutPending_)
    {
        return false;
    }
    // Guard all pages even when the hospital draft is currently hidden. An
    // automatic account-commit logout bypasses this manual navigation path.
    if (!requestAlarmHistoryLeave(tr("注销")) || !requestCalibrationLeave(tr("注销")) || !requestQualityLeave(tr("注销")) || !requestInstrumentLeave(tr("注销")) || !requestCatalogLeave(tr("注销")) || !requestHistoryLeave(tr("注销")) || !userSet_->requestLeave(tr("注销")))
    {
        return false;
    }
    logoutPending_ = true;
    updateAccountStatus();
    const bool queued = QMetaObject::invokeMethod(accounts_, "setSession", Qt::QueuedConnection,
        Q_ARG(QString,QString()),Q_ARG(bool,session_.actionReady));
    if (!queued)
    {
        logoutPending_ = false;
        updateAccountStatus();
        emit navigationRejected(tr("无法提交匿名会话注销请求。"));
    }
    return queued;
}

int frmMain::currentMainPage() const
{
    return ui->stkCenterAdptive->currentIndex();
}

int frmMain::currentUserPage() const
{
    return userSet_->currentUserPage();
}

void frmMain::restoreMainSelection()
{
    if (QAbstractButton *button = menuButtons_->button(currentMainPage()))
    {
        button->setChecked(true);
    }
    ui->lstMenuButton->setCurrentRow(currentMainPage());
}

bool frmMain::selectMainPage(int index)
{
    if (!requestAlarmHistoryLeave(tr("切换页面")))
    {
        restoreMainSelection();
        return false;
    }
    // Export jobs can outlive their preview and can start through the bound
    // controller while another page is visible. Guard the actual job globally.
    if (history_ && history_->isBusy())
    {
        emit navigationRejected(tr("报告数据正在处理或导出，请稍后再切换页面。"));
        restoreMainSelection();
        return false;
    }
    if (!qualityPagesIdle() || !calibrationPageIdle())
    {
        emit navigationRejected(tr("校准/质控本地数据正在处理，请稍后再切换页面。"));
        restoreMainSelection();
        return false;
    }
    if (lisPage() && lisPage()->isBusy())
    {
        emit navigationRejected(tr("LIS设置正在处理，请稍后再切换页面。"));
        restoreMainSelection();
        return false;
    }
    if (rejectLoginNavigation())
    {
        restoreMainSelection();
        return false;
    }
    if (logoutPending_)
    {
        emit navigationRejected(tr("正在注销匿名会话，请稍后再切换页面。"));
        restoreMainSelection();
        return false;
    }
    if (index < 0 || index >= ui->stkCenterAdptive->count())
    {
        emit navigationRejected(tr("主页面索引无效：%1").arg(index));
        restoreMainSelection();
        return false;
    }
    if (index == currentMainPage())
    {
        restoreMainSelection();
        return true;
    }
    if (currentMainPage() == 6
        && !userSet_->requestLeave(tr("切换页面")))
    {
        restoreMainSelection();
        return false;
    }
    if (currentMainPage() == 2 && !requestHistoryLeave(tr("切换页面")))
    {
        restoreMainSelection();
        return false;
    }
    if (!requestCalibrationLeave(tr("切换页面"), true) || !requestQualityLeave(tr("切换页面"), true)
        || (currentMainPage() == 7 && !requestInstrumentLeave(tr("切换页面")))
        || !requestCatalogLeave(tr("切换页面"), true))
    {
        restoreMainSelection();
        return false;
    }
    ui->stkCenterAdptive->setCurrentIndex(index);
    restoreMainSelection();
    if (index == 6 && currentUserPage() == 1)
    {
        userSet_->restoreHospitalFocus();
    }
    if (index == 2)
    {
        updateHistoryStatus();
        restoreHistoryFocus();
    }
    else if ((index == 0 && reagents_) || (index == 1 && samples_) || (index == 5 && projects_))
    {
        updateCatalogStatus();
        restoreCatalogFocus();
    }
    else
    {
        updateHospitalStatus();
        if (index == 4) restoreQualityFocus();
        if (index == 3) restoreCalibrationFocus();
    }
    emit navigationChanged(currentMainPage(),currentUserPage());
    return true;
}

bool frmMain::selectUserPage(int index)
{
    if (!requestAlarmHistoryLeave(tr("切换设置页面"))) return false;
    if (history_ && history_->isBusy())
    {
        emit navigationRejected(tr("报告数据正在处理或导出，请稍后再切换设置页面。"));
        return false;
    }
    if (!qualityPagesIdle() || !calibrationPageIdle())
    {
        emit navigationRejected(tr("校准/质控本地数据正在处理，请稍后再切换设置页面。"));
        return false;
    }
    if (rejectLoginNavigation())
    {
        return false;
    }
    if (logoutPending_)
    {
        emit navigationRejected(tr("正在注销匿名会话，请稍后再切换页面。"));
        return false;
    }
    return userSet_->selectUserPage(index);
}

void frmMain::setDataDirectory(const QString &directory)
{
    hospitalPage()->setDataDirectory(directory);
}

void frmMain::closeEvent(QCloseEvent *event)
{
    if (rejectLoginNavigation())
    {
        event->ignore();
        return;
    }
    if (logoutPending_)
    {
        emit navigationRejected(tr("正在注销匿名会话，请等待结果后再退出。"));
        event->ignore();
        return;
    }
    if (!requestAlarmHistoryLeave(tr("退出")))
    {
        alarmHistoryShutdownPending_ = true;
        event->ignore();
        return;
    }
    if (!requestCalibrationLeave(tr("退出")) || !requestQualityLeave(tr("退出")) || !requestInstrumentLeave(tr("退出")) || !requestCatalogLeave(tr("退出")) || !requestHistoryLeave(tr("退出")) || !userSet_->requestLeave(tr("退出")))
    {
        event->ignore();
        return;
    }
    if (!requestLisRawInboxShutdown())
    {
        event->ignore();
        return;
    }
    // 不关闭外部拥有的存储或终止线程；入口在事件循环返回后按序清理。
    event->accept();
}
