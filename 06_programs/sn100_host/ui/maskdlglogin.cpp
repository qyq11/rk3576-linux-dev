#include "maskdlglogin.h"
#include "ui_maskdlglogin.h"
#include "cusmessagebox.h"
#include "data/account_store.h"

#include <QAtomicInteger>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QRegExpValidator>
#include <QTimer>

namespace
{
QAtomicInteger<quint64> loginRequestSequence(0);

quint64 nextLoginRequest()
{
    // 不同弹窗实例也不重用请求号，旧回复不能落入重新打开的登录卡。
    return loginRequestSequence.fetchAndAddRelaxed(1) + 1;
}
}

MaskDlgLogin::MaskDlgLogin(AccountStore *store, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MaskDlgLogin),
    store_(store)
{
    ui->setupUi(this);
    setObjectName(QStringLiteral("MaskDlgLogin"));
    ui->mask->setupForm(30);
    // 与已迁账户页相同的公开三态主题值；未读取参考配置或猜活动公司名。
    ui->btnLogin->setStyleSheet(QStringLiteral(
        ".QPushButton { background-color: rgba(42,118,242,255); color: #FFFFFF;"
        " border-radius: 4px; padding: 2px; }"
        ".QPushButton:hover { background-color: rgba(42,118,242,170); }"
        ".QPushButton:pressed { background-color: rgba(135,206,250,255); }"));
    // 保留原正则及Intermediate输入可提交行为，不另创用户名长度规则。
    ui->ldtUserName->setValidator(new QRegExpValidator(
        QRegExp(QStringLiteral("^[A-Za-z0-9].{16}$")), ui->ldtUserName));
    ui->ldtUserName->setToolTip(tr("首字母只能是字母或者数字，限制16个字符长度"));
    connect(ui->btnLogin, &QPushButton::clicked, this, &MaskDlgLogin::submitLogin);
    connect(ui->mask, &CusDialogMask::closeFinished, this, [this]()
    {
        if (!closing_)
        {
            return;
        }
        closing_ = false;
        finishing_ = true;
        ui->ldtPassword->clear();
        emit busyChanged(false);
        QDialog::done(result_);
    });
    if (store_)
    {
        connect(store_, &AccountStore::defaultLoginLoaded, this,
            [this](quint64 requestId, const QString &name, const QString &warning)
        {
            if (!requestId_ || requestId != requestId_ || !loadingDefault_ || !shown_ || closing_)
            {
                return;
            }
            requestId_ = 0;
            loadingDefault_ = false;
            ui->ldtUserName->setText(name);
            setBusy(false);
            if (!warning.isEmpty())
            {
                showMessage(warning, false);
            }
        });
        connect(store_, &AccountStore::loginFinished, this,
            [this](quint64 requestId, bool accepted, bool, const QString &error, const QString &warning)
        {
            if (!requestId_ || requestId != requestId_ || loadingDefault_ || !shown_ || closing_)
            {
                return;
            }
            requestId_ = 0;
            setBusy(false);
            if (!accepted)
            {
                showMessage(error, false);
            }
            else if (!warning.isEmpty())
            {
                // 认证已成功；确认默认保存警告后正常关闭，不能伪装为拒绝登录。
                showMessage(warning, true);
            }
            else
            {
                finishAnimated(QDialog::Accepted);
            }
        });
        connect(store_, &QObject::destroyed, this, [this]()
        {
            requestId_ = 0;
            setBusy(false);
            if (isVisible() && !closing_ && !message_)
            {
                showMessage(tr("账户服务已关闭，请重新打开程序。"), false);
            }
        });
    }
}

MaskDlgLogin::~MaskDlgLogin()
{
    ui->ldtPassword->clear();
    delete ui;
}

bool MaskDlgLogin::isBusy() const
{
    return busy_ || closing_ || !message_.isNull();
}

void MaskDlgLogin::setBusy(bool busy)
{
    busy_ = busy;
    const bool editable = !isBusy();
    ui->ldtUserName->setEnabled(editable);
    ui->ldtPassword->setEnabled(editable);
    ui->cbx_isdefault->setEnabled(editable);
    ui->btnLogin->setEnabled(editable);
    emit busyChanged(isBusy());
}

void MaskDlgLogin::showDialog()
{
    if (shown_)
    {
        return;
    }
    shown_ = true;
    ui->ldtUserName->setText(QStringLiteral("Guest"));
    ui->ldtPassword->clear();
    ui->cbx_isdefault->setChecked(false);
    requestId_ = nextLoginRequest();
    loadingDefault_ = true;
    setBusy(true);
    open();
    ui->mask->showAnimation();
    const bool queued = store_ && QMetaObject::invokeMethod(store_, "loadLoginDefault", Qt::QueuedConnection,
        Q_ARG(quint64, requestId_));
    if (!queued)
    {
        requestId_ = 0;
        setBusy(false);
        showMessage(tr("账户服务不可用，未建立登录会话。"), false);
    }
    QTimer::singleShot(300, this, [this]()
    {
        if (isVisible() && !isBusy())
        {
            ui->ldtUserName->setFocus();
        }
    });
}

void MaskDlgLogin::submitLogin()
{
    if (!shown_ || isBusy() || !ui->mask->isReadyForInput())
    {
        return;
    }
    requestId_ = nextLoginRequest();
    loadingDefault_ = false;
    setBusy(true);
    const bool queued = store_ && QMetaObject::invokeMethod(store_, "authenticate", Qt::QueuedConnection,
        Q_ARG(quint64, requestId_), Q_ARG(QString, ui->ldtUserName->text()),
        Q_ARG(QString, ui->ldtPassword->text()), Q_ARG(bool, ui->cbx_isdefault->isChecked()));
    if (!queued)
    {
        requestId_ = 0;
        setBusy(false);
        showMessage(tr("登录请求未进入账户工作线程，请稍后重试。"), false);
    }
}

void MaskDlgLogin::showMessage(const QString &message, bool acceptAfterAcknowledged)
{
    if (message_ || closing_)
    {
        return;
    }
    auto *dialog = new CusMessageBox(this);
    dialog->setObjectName(QStringLiteral("loginWarningDialog"));
    message_ = dialog;
    setBusy(false);
    connect(dialog, &QDialog::finished, this, [this, dialog, acceptAfterAcknowledged]()
    {
        message_.clear();
        dialog->deleteLater();
        setBusy(false);
        if (acceptAfterAcknowledged)
        {
            finishAnimated(QDialog::Accepted);
        }
    });
    dialog->openWarningBox(message);
}

void MaskDlgLogin::finishAnimated(int result)
{
    if (closing_ || message_)
    {
        return;
    }
    requestId_ = 0;
    result_ = result;
    closing_ = true;
    setBusy(false);
    ui->mask->closeAnimation();
}

void MaskDlgLogin::reject()
{
    if (!isBusy())
    {
        finishAnimated(QDialog::Rejected);
    }
}

void MaskDlgLogin::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
    {
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

void MaskDlgLogin::closeEvent(QCloseEvent *event)
{
    if (finishing_)
    {
        event->accept();
        return;
    }
    event->ignore();
    reject();
}
