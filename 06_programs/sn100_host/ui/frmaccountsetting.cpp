// Source: reference frmuserset/frmaccountsetting.{h,cpp,ui}. Geometry, names,
// labels and original local/dynamic styles are retained; only the unavailable
// PingFang family is replaced with Noto Sans CJK SC. The two original root
// groups move together horizontally when the independent host is resized.
// Data/authentication checks run in AccountStore's worker thread. The explicit
// offline session replaces SystemCommon globals; no login/device objects run.
#include "frmaccountsetting.h"
#include "ui_frmaccountsetting.h"
#include "cusmessagebox.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QFont>
#include <QLineEdit>
#include <QListView>
#include <QRegExp>
#include <QRegExpValidator>
#include <QResizeEvent>
#include <QTableWidget>

frmAccountSetting::frmAccountSetting(AccountStore *store, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::frmAccountSetting)
{
    ui->setupUi(this);
    // Original frmUserSet supplied a white background and inherited button QSS.
    // Restore that context for this independent page, without adding controls.
    setStyleSheet(QStringLiteral(
        "QWidget#frmAccountSetting { background-color: white; }"
        ".QPushButton { background-color: rgba(42,118,242,255); color: #FFFFFF;"
        " border-radius: 4px; padding: 2px; }"
        ".QPushButton:hover { background-color: rgba(42,118,242,170); }"
        ".QPushButton:pressed { background-color: rgba(135,206,250,255); }"));
    // Original reference theme_config.ini visual values, not a claim about
    // the active Windows release theme. No reference config is opened at run time.
    ui->wgtTabs_2->setStyleSheet(QStringLiteral(
        ".QPushButton { font: bold 18px \"Noto Sans CJK SC\"; color: #AAAAAA;"
        " border: none; background-color: transparent;"
        " border-bottom: 5px solid transparent; }"
        ".QPushButton:checked { color: #444444;"
        " border-bottom: 5px solid rgba(232,51,35,255); }"));
    const QString comboStyle = QStringLiteral(
        "QComboBox { border: 1px solid #E4E8EB; border-radius: 5px; padding: 2px 4px;"
        " background-color: transparent; color: #444444; font: 18px \"Noto Sans CJK SC\"; }"
        "QComboBox:disabled { background-color: #E4E4E4; }"
        "QComboBox QAbstractItemView { border: none; color: #444444; font: 20px \"Noto Sans CJK SC\"; }"
        "QComboBox QAbstractItemView::item { height: 50px; }"
        "QComboBox QAbstractItemView::item:selected { background: rgba(183,215,255,255); color: white; }"
        "QComboBox::drop-down { width: 20px; border: none; }"
        "QComboBox::down-arrow { width: 25px; height: 25px; image: url(:/image/arrow_drop_down.svg); }");
    ui->cbxTypeSelectAdd->setView(new QListView());
    ui->cbxTypeSelectUpdate->setView(new QListView());
    ui->cbxTypeSelectAdd->setStyleSheet(comboStyle);
    ui->cbxTypeSelectUpdate->setStyleSheet(comboStyle);
    ui->stkAccount->setCurrentIndex(0);
    ui->btnMenuAdd->setChecked(true);

    // Preserve SystemCommon::lineValidator_Head_Letter_Num exactly. The source
    // calls this "16 characters" but its regex includes one leading character
    // plus 16 following characters; shorter intermediate values can be submitted.
    const QList<QLineEdit *> userNames = {ui->ldtUserNameUpdate, ui->ldtUserNameUpdate2,
                                         ui->ldtUserNameAdd, ui->ldtUserNameDel};
    for (QLineEdit *edit : userNames)
    {
        edit->setValidator(new QRegExpValidator(QRegExp(QStringLiteral("^[A-Za-z0-9].{16}$")), edit));
        edit->setToolTip(tr("首字母只能是字母或者数字，限制16个字符长度"));
    }
    // The original reconnects this once per row and refresh. One connection has
    // the same result without multiplying slot invocations after every write.
    connect(ui->tbl_Account, &QTableWidget::cellDoubleClicked,
            this, &frmAccountSetting::slotSetText);

    connect(this, &frmAccountSetting::requestLoad,
            store, &AccountStore::load, Qt::QueuedConnection);
    connect(this, &frmAccountSetting::requestAdd,
            store, &AccountStore::add, Qt::QueuedConnection);
    connect(this, &frmAccountSetting::requestRemove,
            store, &AccountStore::remove, Qt::QueuedConnection);
    connect(this, &frmAccountSetting::requestUpdate,
            store, &AccountStore::update, Qt::QueuedConnection);
    connect(store, &AccountStore::ready, this, [this]()
    {
        ready_ = true;
        beginRead(QStringLiteral("initialize"));
    });
    connect(store, &AccountStore::sessionChanged, this, [this](const AccountSession &session)
    {
        session_ = session;
        if (!session_.authenticated)
        {
            resetForLogout();
        }
        slotUserChanged();
        // A role recheck can arrive while a write is busy and later rejected.
        // Refilter the last public snapshot immediately; do not leave a former
        // administrator's rows visible or overwrite the in-flight operation.
        frmGetAccount(displayedRows_);
    });
    connect(store, &AccountStore::loaded, this, [this](const AccountRows &rows)
    {
        // A post-logout load can complete after identity is already cleared.
        // Keep neither visible rows nor a former user's list cache as Guest.
        displayedRows_ = session_.authenticated && session_.role != 0 ? rows : AccountRows();
        frmGetAccount(displayedRows_);
        if (!awaitingLoad_)
        {
            return;
        }
        awaitingLoad_ = false;
        const QString message = successMessage_.isEmpty()
                ? tr("账户列表已刷新。") : successMessage_;
        reportStatus(message);
        if (!successMessage_.isEmpty())
        {
            CusMessageBox dialog(this);
            dialog.setObjectName(QStringLiteral("accountInformationDialog"));
            dialog.showInformationBox(message);
        }
        successMessage_.clear();
        setBusy(false);
        emit operationFinished(pendingOperation_, true);
    });
    connect(store, &AccountStore::succeeded, this, [this](const QString &operation, bool loggedOut)
    {
        pendingOperation_ = operation;
        if (operation == QStringLiteral("add"))
        {
            successMessage_ = tr("保存成功!");
            ui->ldtPasswordAdd->clear();
            ui->ldtPasswordAgainAdd->clear();
            ui->ldtUserNameAdd->clear();
            ui->cbxTypeSelectAdd->setCurrentIndex(0);
        }
        else if (operation == QStringLiteral("remove"))
        {
            successMessage_ = tr("删除成功!");
            ui->ldtUserNameDel->clear();
            ui->ldtPasswordDel->clear();
        }
        else
        {
            successMessage_ = tr("更新成功!");
            ui->ldtUserPasswordUpdate->clear();
            ui->ldtUserPasswordUpdate2->clear();
            ui->ldtUserPasswordUpdate3->clear();
        }
        if (loggedOut)
        {
            // The preceding AccountStore::sessionChanged has already cleared
            // identity. Retain this legacy notification for the independent
            // O02 host; an embedded shell observes the store's actual session.
            emit signalLoginOut();
        }
        awaitingLoad_ = true;
        reportStatus(tr("%1 正在重新读取账户列表…").arg(successMessage_));
        emit requestLoad();
    });
    connect(store, &AccountStore::error, this, [this](const QString &operation, const QString &message)
    {
        const bool committed = awaitingLoad_ && !successMessage_.isEmpty();
        awaitingLoad_ = false;
        successMessage_.clear();
        const QString reported = committed
                ? tr("账户操作已提交，但列表重读失败：%1。可切换模拟会话后重读。").arg(message)
                : message;
        // A committed write is never represented as a failed transaction; the
        // message distinguishes reload failure and avoids blind write retries.
        rejectOperation(committed ? pendingOperation_ : operation, reported);
    });
    refreshRoleOptions();
    ui->tbl_Account->hide();
    setMinimumSize(1061, 861);
    setBusy(true);
}

frmAccountSetting::~frmAccountSetting()
{
    clearPasswords();
    delete ui;
}

void frmAccountSetting::setBusy(bool busy)
{
    const bool changed = busy_ != busy;
    busy_ = busy;
    const bool enabled = ready_ && !busy;
    ui->widget->setEnabled(enabled);
    ui->tbl_Account->setEnabled(enabled);
    if (changed)
    {
        emit busyChanged(busy);
    }
}

void frmAccountSetting::setEmbeddedInShell()
{
    embeddedInShell_ = true;
    ui->tbl_Account->move(10, 30);
    ui->widget->move(410, 30);
}

bool frmAccountSetting::requestLeave(const QString &action)
{
    if (busy_)
    {
        reportStatus(tr("账户数据正在处理，请等待结果后再%1。").arg(action), true);
        return false;
    }
    // User names and selected tabs can remain as an unsubmitted draft, but
    // passwords must not survive leaving this page.
    clearPasswords();
    return true;
}

void frmAccountSetting::resetForLogout()
{
    clearPasswords();
    ui->ldtUserNameAdd->clear();
    ui->ldtUserNameDel->clear();
    ui->ldtUserNameUpdate->clear();
    ui->ldtUserNameUpdate2->clear();
    displayedRows_.clear();
    ui->tbl_Account->clearContents();
    ui->tbl_Account->setRowCount(0);
    ui->tbl_Account->hide();
    refreshRoleOptions();
}

void frmAccountSetting::reportStatus(const QString &message, bool error)
{
    status_ = message;
    emit statusChanged(message, error);
}

void frmAccountSetting::rejectOperation(const QString &operation, const QString &message)
{
    reportStatus(message, true);
    CusMessageBox dialog(this);
    dialog.setObjectName(QStringLiteral("accountErrorDialog"));
    const bool storageError = operation == QStringLiteral("initialize")
            || operation == QStringLiteral("load") || message.contains(QStringLiteral("事务"))
            || message.contains(QStringLiteral("Unable to"))
            || message.contains(QStringLiteral("SQLite"), Qt::CaseInsensitive)
            || message.contains(QStringLiteral("database"), Qt::CaseInsensitive);
    if (storageError)
    {
        dialog.showErrorBox(message);
    }
    else
    {
        dialog.showWarningBox(message);
    }
    setBusy(false);
    emit operationFinished(operation, false);
}

void frmAccountSetting::beginRead(const QString &operation)
{
    if (!ready_)
    {
        return;
    }
    pendingOperation_ = operation;
    successMessage_.clear();
    awaitingLoad_ = true;
    setBusy(true);
    reportStatus(tr("正在读取账户列表…"));
    emit requestLoad();
}

void frmAccountSetting::refreshRoleOptions()
{
    ui->cbxTypeSelectAdd->clear();
    ui->cbxTypeSelectUpdate->clear();
    const int highestRole = qMin(session_.role, 4);
    for (int role = 1; role <= highestRole; ++role)
    {
        const QString roleName = AccountStore::roleName(role);
        ui->cbxTypeSelectAdd->addItem(roleName, role);
        ui->cbxTypeSelectUpdate->addItem(roleName, role);
    }
    // Source behavior: Administrator (5) has only four options. Index 4 is
    // invalid, leaving an empty selection whose original role lookup returns 0.
    ui->cbxTypeSelectUpdate->setCurrentIndex(session_.role - 1);
    ui->ldtUserNameUpdate->setText(session_.name);
    ui->ldtUserNameUpdate2->setText(session_.name);
}

void frmAccountSetting::slotUserChanged()
{
    clearPasswords();
    refreshRoleOptions();
    if (!session_.authenticated || session_.role == 0)
    {
        ui->tbl_Account->hide();
    }
    if (ready_ && !busy_)
    {
        beginRead(QStringLiteral("session"));
    }
}

void frmAccountSetting::frmGetAccount(const AccountRows &rows)
{
    ui->tbl_Account->clearContents();
    ui->tbl_Account->setRowCount(0);
    if (session_.role == 0)
    {
        ui->tbl_Account->hide();
        return;
    }
    ui->tbl_Account->show();
    ui->tbl_Account->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tbl_Account->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tbl_Account->setSelectionMode(QAbstractItemView::NoSelection);
    ui->tbl_Account->setFocusPolicy(Qt::NoFocus);
    ui->tbl_Account->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    // Original visibility is unusual: roles 1..4 see roles below Engineer;
    // Administrator sees roles below Administrator. Preserve that exact test.
    for (const AccountRow &row : rows)
    {
        if (row.role >= session_.role && row.role >= 4)
        {
            continue;
        }
        const int index = ui->tbl_Account->rowCount();
        ui->tbl_Account->insertRow(index);
        const QStringList values = {QString::number(index + 1), AccountStore::roleName(row.role), row.name};
        for (int column = 0; column < values.size(); ++column)
        {
            auto *item = new QTableWidgetItem(values[column]);
            item->setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 13));
            item->setTextAlignment(Qt::AlignCenter);
            ui->tbl_Account->setItem(index, column, item);
        }
        ui->tbl_Account->setRowHeight(index, 41);
    }
    ui->tbl_Account->setVerticalHeaderLabels({tr("序号"), tr("用户类型"), tr("用户名")});
}

void frmAccountSetting::slotSetText(int row, int column)
{
    Q_UNUSED(column);
    auto *item = ui->tbl_Account->item(row, ui->tbl_Account->columnCount() - 1);
    if (!item || busy_)
    {
        return;
    }
    // Original double-click also replaces read-only update names, including
    // another visible user. It does not change the chosen role or passwords.
    ui->ldtUserNameAdd->setText(item->text());
    ui->ldtUserNameDel->setText(item->text());
    ui->ldtUserNameUpdate->setText(item->text());
    ui->ldtUserNameUpdate2->setText(item->text());
}

int frmAccountSetting::selectedRole(const QComboBox *combo) const
{
    return combo->currentIndex() < 0 ? 0 : combo->currentData().toInt();
}

void frmAccountSetting::on_btnMenuAdd_clicked()
{
    ui->stkAccount->setCurrentIndex(0);
    emit operationFinished(QStringLiteral("page"), true);
}

void frmAccountSetting::on_btnMenuDel_clicked()
{
    ui->stkAccount->setCurrentIndex(1);
    emit operationFinished(QStringLiteral("page"), true);
}

void frmAccountSetting::on_btnMenuChange_clicked()
{
    ui->stkAccount->setCurrentIndex(2);
    ui->ldtUserNameUpdate->setReadOnly(true);
    ui->ldtUserNameUpdate2->setReadOnly(true);
    ui->ldtUserNameUpdate->setText(session_.name);
    ui->ldtUserNameUpdate2->setText(session_.name);
    ui->cbxTypeSelectUpdate->setCurrentIndex(session_.role - 1);
    emit operationFinished(QStringLiteral("page"), true);
}

void frmAccountSetting::on_btnAdd_clicked()
{
    if (busy_ || !ready_)
    {
        return;
    }
    if (session_.role == 0)
    {
        rejectOperation(QStringLiteral("add"), tr("增加失败: 无权限!"));
        return;
    }
    if (ui->ldtUserNameAdd->text().isEmpty() || ui->ldtPasswordAgainAdd->text().isEmpty())
    {
        rejectOperation(QStringLiteral("add"), tr("增加失败: 用户名或密码不能为空!"));
        return;
    }
    if (ui->ldtPasswordAdd->text() != ui->ldtPasswordAgainAdd->text())
    {
        rejectOperation(QStringLiteral("add"), tr("增加失败: 密码不一致,请重新输入!"));
        return;
    }
    if (ui->cbxTypeSelectAdd->currentIndex() == 4)
    {
        rejectOperation(QStringLiteral("add"), tr("增加失败: 不能添加管理员!"));
        return;
    }
    pendingOperation_ = QStringLiteral("add");
    setBusy(true);
    reportStatus(tr("正在保存账户…"));
    emit requestAdd(ui->ldtUserNameAdd->text(), ui->ldtPasswordAdd->text(),
                    ui->ldtPasswordAgainAdd->text(), selectedRole(ui->cbxTypeSelectAdd));
}

void frmAccountSetting::on_btnDel_clicked()
{
    if (busy_ || !ready_)
    {
        return;
    }
    CusMessageBox dialog(this);
    dialog.setObjectName(QStringLiteral("accountDeleteDialog"));
    dialog.showQuestionBox(tr("确认删除用户？"));
    if (dialog.getClickButton() != QMessageBox::Yes)
    {
        reportStatus(tr("已取消删除用户。"));
        emit operationFinished(QStringLiteral("remove"), false);
        return;
    }
    if (ui->ldtUserNameDel->text() == session_.name && !session_.actionReady)
    {
        rejectOperation(QStringLiteral("remove"), tr("设备正在运行，不允许删除正在登陆账户!"));
        return;
    }
    pendingOperation_ = QStringLiteral("remove");
    setBusy(true);
    reportStatus(tr("正在删除账户…"));
    emit requestRemove(ui->ldtUserNameDel->text(), ui->ldtPasswordDel->text());
}

void frmAccountSetting::on_btnUpdate_clicked()
{
    if (busy_ || !ready_)
    {
        return;
    }
    // Intentionally preserve the original condition, which conflicts with its
    // comment: running + unchanged role index is rejected. Do not invert it.
    if (ui->cbxTypeSelectUpdate->currentIndex() == session_.role - 1 && !session_.actionReady)
    {
        rejectOperation(QStringLiteral("update"), tr("设备正在运行，不允许修改用户类型!"));
        return;
    }
    pendingOperation_ = QStringLiteral("update");
    setBusy(true);
    reportStatus(tr("正在更新账户…"));
    // Existing password, confirmation, name and empty-new-password behavior
    // are checked in original order by the worker, without exposing stored pwd.
    emit requestUpdate(ui->ldtUserNameUpdate->text(), ui->ldtUserPasswordUpdate->text(),
                       ui->ldtUserNameUpdate2->text(), ui->ldtUserPasswordUpdate2->text(),
                       ui->ldtUserPasswordUpdate3->text(), selectedRole(ui->cbxTypeSelectUpdate));
}

void frmAccountSetting::clearPasswords()
{
    const QList<QLineEdit *> passwords = {ui->ldtPasswordAdd, ui->ldtPasswordAgainAdd,
        ui->ldtPasswordDel, ui->ldtUserPasswordUpdate, ui->ldtUserPasswordUpdate2,
        ui->ldtUserPasswordUpdate3};
    for (QLineEdit *edit : passwords)
    {
        edit->clear();
    }
}

void frmAccountSetting::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // The source's left group spans x=10..401 and the right x=410..1051.
    // Translate both together, preserving every business-relative coordinate.
    const int horizontalOffset = embeddedInShell_ ? 0 : qMax(0, (width() - 1061) / 2);
    ui->tbl_Account->move(10 + horizontalOffset, 30);
    ui->widget->move(410 + horizontalOffset, 30);
}

void frmAccountSetting::closeEvent(QCloseEvent *event)
{
    if (busy_)
    {
        reportStatus(tr("正在处理账户数据，请等待结果后再关闭窗口。"), true);
        event->ignore();
        return;
    }
    clearPasswords();
    QWidget::closeEvent(event);
}
