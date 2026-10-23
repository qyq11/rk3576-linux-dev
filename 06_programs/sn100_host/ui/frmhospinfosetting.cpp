// Source: read-only reference frmuserset/frmhospinfosetting.{h,cpp,ui}.
// Retained: five dictionary types, table columns, original object names, automatic
// initials, add/edit/save/cancel/delete and the hospital/name compatibility key.
// Original panel coordinates, sizes, ordering and local/shared QSS are retained.
// 独立宿主居中放置原 801x551 业务区域，脱机说明位于该区域之外。
// 当前使用已安装的 Noto 字体并保留原字号；参考工程含 PingFangBold.ttf，
// 其引入条件和实际字形度量尚待核实，不能据此认定原字体在 Linux 不可用。
// Intentional changes: queued storage, atomic store writes, explicit discard
// confirmation and visible non-modal results. Physical
// keyboard input is used; instrument, login, soft-keyboard and algorithm pages
// are outside this offline module and are not instantiated here.
#include "frmhospinfosetting.h"
#include "ui_frmhospinfosetting.h"
#include "mnemonic.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QEvent>
#include <QHeaderView>
#include <QLineEdit>
#include <QMessageBox>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QTableWidget>

frmHospInfoSetting::frmHospInfoSetting(HospitalStore *store, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::frmHospInfoSetting),
    categoryButtons_(new QButtonGroup(this))
{
    ui->setupUi(this);
    ui->statusLabel->setTextFormat(Qt::PlainText);
    ui->dataPathLabel->setTextFormat(Qt::PlainText);
    ui->tableInfo->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableInfo->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableInfo->verticalHeader()->setVisible(false);
    ui->tableInfo->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableInfo->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    ui->tableInfo->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    // Shipped reference theme visual keys from bin/theme_config.ini, applied
    // by main.cpp:getConfig(). The active Windows installation theme remains
    // unverified. No reference INI or production config is loaded at runtime.
    const QString matchColor = QStringLiteral("232,51,35,255");
    const QString rowSelectionColor = QStringLiteral("183,215,255,255");
    const QString buttonNormalColor = QStringLiteral("42,118,242,255");
    const QString buttonHoverColor = QStringLiteral("42,118,242,170");
    const QString buttonPressedColor = QStringLiteral("135,206,250,255");

    // Original inherited buttonStyle::setStrSheet(), scoped to the business
    // panel; the experimental footer is separate from the product UI.
    ui->widget->setStyleSheet(ui->widget->styleSheet() + QStringLiteral(
        ".QPushButton { background-color: rgba(%1); color: #FFFFFF;"
        " border-radius: 4px; padding: 2px; }"
        ".QPushButton:hover { background-color: rgba(%2); }"
        ".QPushButton:pressed { background-color: rgba(%3); }")
        .arg(buttonNormalColor, buttonHoverColor, buttonPressedColor));
    // Original frmHospInfoSetting constructor's dynamic tab style.
    ui->wgtTabs->setStyleSheet(QStringLiteral(
        ".QPushButton { font: bold 18px \"Noto Sans CJK SC\"; color: #AAAAAA;"
        " border: none; background-color: transparent;"
        " border-bottom: 5px solid transparent; }"
        ".QPushButton:checked { color: #444444; border-bottom: 5px solid rgba(%1); }")
        .arg(matchColor));
    // 原 public/stylecolor.cpp:tableStyle::getStrSheet6()；当前字体替代差异
    // 单独登记待对照，表头和表格主体仍保留原透明背景。
    ui->tableInfo->setStyleSheet(QStringLiteral(
        "QHeaderView { background-color: transparent; }"
        "QHeaderView::section { color: #444444; font: bold 15px \"Noto Sans CJK SC\";"
        " text-align: left; height: 40px; background-color: transparent;"
        " border: 1px solid #E4E8EB; border-left: none; border-right: none; border-top: none; }"
        "QTableWidget::Item { border: 0px solid #DDDDDD; border-bottom: 1px solid #DDDDDD; color: #444444; }"
        "QTableView::item:selected { background: rgba(%1); }"
        "QTableWidget { background-color: transparent; border: 1px solid #E4E8EB; border-radius: 5px; }"
        "QLineEdit { background-color: transparent; border: 0px solid #DDDDDD; border-bottom: 1px solid #DDDDDD; }"
        "QLineEdit:selected { background: rgba(%1); }").arg(rowSelectionColor));

    const QList<QPair<QAbstractButton *, QString>> categories = {
        {ui->btnKeShiInfo, QStringLiteral("keshi")},
        {ui->btnBingRenInfo, QStringLiteral("bingren")},
        {ui->btnSongJianInfo, QStringLiteral("songjian")},
        {ui->btnJianYanInfo, QStringLiteral("jianyan")},
        {ui->btnShenHeInfo, QStringLiteral("shenhe")}
    };
    for (const auto &category : categories)
    {
        categoryButtons_->addButton(category.first);
        category.first->setProperty("dictionaryType", category.second);
    }
    categoryButtons_->setExclusive(true);
    connect(categoryButtons_, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
            this, &frmHospInfoSetting::selectCategory);
    connect(ui->ldtHospName, &QLineEdit::textChanged, this, [this]() { setDirty(); });

    connect(this, &frmHospInfoSetting::requestLoad,
            store, &HospitalStore::load, Qt::QueuedConnection);
    connect(this, &frmHospInfoSetting::requestSave,
            store, &HospitalStore::save, Qt::QueuedConnection);
    connect(this, &frmHospInfoSetting::requestRemove,
            store, &HospitalStore::remove, Qt::QueuedConnection);
    connect(store, &HospitalStore::ready, this, [this]()
    {
        ready_ = true;
        beginLoad(QStringLiteral("initialize"), tr("正在读取脱机测试数据…"));
    });
    connect(store, &HospitalStore::loaded, this,
            [this](const QString &type, const QString &hospitalName, const HospitalRows &rows)
    {
        if (type != type_)
        {
            return;
        }
        renderRows(hospitalName, rows);
        setBusy(false);
        QString message = reloadMessage_.isEmpty()
                ? tr("已读取 %1 条记录。仅使用脱机测试数据。").arg(rows.size())
                : reloadMessage_ + tr("；已重新读取 %1 条记录。").arg(rows.size());
        if (!operationWarning_.isEmpty())
        {
            message += tr(" 注意：%1").arg(operationWarning_);
        }
        showStatus(message);
        reloadMessage_.clear();
        emit operationFinished(pendingOperation_, true);
    });
    connect(store, &HospitalStore::saved, this,
            [this](const QString &type, const QString &hospitalName, const HospitalRows &rows)
    {
        if (type != type_)
        {
            return;
        }
        // Accept committed IDs even if the independent verification read fails.
        renderRows(hospitalName, rows);
        reloadMessage_ = tr("保存完成");
        showStatus(tr("保存已提交，正在重新读取核对…"));
        emit requestLoad(type_);
    });
    connect(store, &HospitalStore::removed, this,
            [this](const QString &type, qint64 id)
    {
        if (type != type_)
        {
            return;
        }
        HospitalRows remaining;
        for (const HospitalRow &row : displayedRows_)
        {
            if (row.id != id)
            {
                remaining.append(row);
            }
        }
        renderRows(displayedHospitalName_, remaining);
        reloadMessage_ = tr("删除完成");
        showStatus(tr("删除已提交，正在重新读取核对…"));
        emit requestLoad(type_);
    });
    connect(store, &HospitalStore::warning, this, [this](const QString &message)
    {
        operationWarning_ = message;
        showStatus(tr("数据操作提示：%1").arg(message));
    });
    connect(store, &HospitalStore::error, this,
            [this](const QString &operation, const QString &message)
    {
        // A failed category load must not relabel the still-visible draft.
        if (operation == QStringLiteral("load") && type_ != displayedType_)
        {
            type_ = displayedType_;
            checkCurrentCategory();
        }
        setBusy(false);
        reloadMessage_.clear();
        if (operation == QStringLiteral("load")
                && (pendingOperation_ == QStringLiteral("save") || pendingOperation_ == QStringLiteral("remove")))
        {
            showStatus(tr("数据已提交；再次读取核对失败：%1。当前显示已提交的数据，可点击刷新重试。").arg(message), true);
        }
        else
        {
            showStatus(tr("操作失败（%1）：%2").arg(operation, message), true);
        }
        emit operationFinished(pendingOperation_, false);
    });
    setBusy(true);
}

frmHospInfoSetting::~frmHospInfoSetting()
{
    delete ui;
}

void frmHospInfoSetting::setDataDirectory(const QString &directory)
{
    ui->dataPathLabel->setText(tr("独立测试数据目录：%1").arg(directory));
}

bool frmHospInfoSetting::isDoctorType() const
{
    return type_ == QStringLiteral("songjian") || type_ == QStringLiteral("jianyan")
            || type_ == QStringLiteral("shenhe");
}

void frmHospInfoSetting::setBusy(bool busy)
{
    busy_ = busy;
    const bool enabled = ready_ && !busy_;
    ui->ldtHospName->setEnabled(enabled);
    ui->tableInfo->setEnabled(enabled);
    ui->btnAdd->setEnabled(enabled);
    ui->btnDel->setEnabled(enabled);
    ui->btnSave->setEnabled(enabled);
    ui->btnCancel->setEnabled(enabled);
    ui->btnReload->setEnabled(enabled);
    for (QAbstractButton *button : categoryButtons_->buttons())
    {
        button->setEnabled(enabled);
    }
}

void frmHospInfoSetting::setDirty()
{
    if (!rendering_)
    {
        dirty_ = true;
        showStatus(tr("有未保存的更改；点击“保存”写入测试数据，或“取消”重新读取。"));
    }
}

void frmHospInfoSetting::showStatus(const QString &message, bool error)
{
    ui->statusLabel->setText(message);
    ui->statusLabel->setProperty("isError", error);
    // Experimental feedback remains outside the original business panel.
    ui->statusLabel->setStyleSheet(error
        ? QStringLiteral("QLabel { color: rgb(232,51,35); }")
        : QStringLiteral("QLabel { color: #444444; }"));
    emit statusChanged(message, error);
}

void frmHospInfoSetting::setEmbeddedInShell()
{
    // Keep the original panel coordinates inside the original user-settings
    // stack. The standalone experiment footer is not part of the product page.
    ui->offlineHostLayout->removeWidget(ui->widget);
    ui->offlineFooter->hide();
    setMinimumSize(0, 0);
    ui->widget->setGeometry(330, 10, 801, 551);
    ui->widget->show();
}

QString frmHospInfoSetting::statusText() const
{
    return ui->statusLabel->text();
}

bool frmHospInfoSetting::requestLeave(const QString &action)
{
    if (busy_)
    {
        showStatus(tr("数据操作正在进行，请等待结果后再%1。").arg(action));
        return false;
    }
    if (!confirmDiscard(action))
    {
        return false;
    }
    if (dirty_)
    {
        // A hidden page must not keep a discarded draft. Use the last successful
        // load/commit snapshot; no database write or asynchronous reload occurs.
        const QString hospitalName = displayedHospitalName_;
        const HospitalRows rows = displayedRows_;
        renderRows(hospitalName, rows);
        showStatus(tr("已放弃未保存的更改，恢复上次成功读取或提交的数据。"));
    }
    return true;
}

bool frmHospInfoSetting::confirmDiscard(const QString &action)
{
    if (!dirty_)
    {
        return true;
    }
    QMessageBox dialog(QMessageBox::Question, tr("确认放弃未保存的更改"),
                       tr("%1将放弃当前未保存的更改，是否继续？").arg(action),
                       QMessageBox::Yes | QMessageBox::No, this);
    dialog.setObjectName(QStringLiteral("discardChangesDialog"));
    dialog.setDefaultButton(QMessageBox::No);
    dialog.setButtonText(QMessageBox::Yes, tr("放弃并继续"));
    dialog.setButtonText(QMessageBox::No, tr("保留更改"));
    // The embedded page inherits the original button QSS. Fusion's standard
    // icon plus CJK text otherwise clips the last character on Linux.
    for (QAbstractButton *button : dialog.buttons())
    {
        const int iconWidth = button->icon().isNull() ? 0 : button->iconSize().width() + 8;
        button->setMinimumWidth(button->fontMetrics().horizontalAdvance(button->text()) + iconWidth + 24);
    }
    dialog.adjustSize();
    return dialog.exec() == QMessageBox::Yes;
}

void frmHospInfoSetting::checkCurrentCategory()
{
    for (QAbstractButton *button : categoryButtons_->buttons())
    {
        if (button->property("dictionaryType").toString() == type_)
        {
            button->setChecked(true);
            return;
        }
    }
}

void frmHospInfoSetting::selectCategory(QAbstractButton *button)
{
    const QString selectedType = button->property("dictionaryType").toString();
    if (selectedType == type_)
    {
        checkCurrentCategory();
        return;
    }
    if (!confirmDiscard(tr("切换信息类别")))
    {
        checkCurrentCategory();
        return;
    }
    type_ = selectedType;
    beginLoad(QStringLiteral("load"), tr("正在读取所选类别…"));
}

void frmHospInfoSetting::beginLoad(const QString &operation, const QString &message)
{
    pendingOperation_ = operation;
    reloadMessage_.clear();
    if (operation != QStringLiteral("initialize"))
    {
        operationWarning_.clear();
    }
    setBusy(true);
    showStatus(message);
    emit requestLoad(type_);
}

void frmHospInfoSetting::renderRows(const QString &hospitalName, const HospitalRows &rows)
{
    displayedType_ = type_;
    displayedHospitalName_ = hospitalName;
    displayedRows_ = rows;
    rendering_ = true;
    ui->ldtHospName->setText(hospitalName);
    ui->tableInfo->clear();
    ui->tableInfo->setRowCount(0);
    const QString name = isDoctorType() ? tr("医生姓名")
            : (type_ == QStringLiteral("keshi") ? tr("科室名称") : tr("病人类型"));
    ui->tableInfo->setColumnCount(6);
    ui->tableInfo->setHorizontalHeaderLabels({name, tr("助记符"),
        isDoctorType() ? tr("工号") : tr("备注"), QStringLiteral("id"),
        QStringLiteral("type"), QStringLiteral("flag")});
    for (int column = 3; column < 6; ++column)
    {
        ui->tableInfo->setColumnHidden(column, true);
    }
    for (const HospitalRow &row : rows)
    {
        addLineData(row, false);
    }
    if (!rows.isEmpty())
    {
        ui->tableInfo->selectRow(0);
    }
    dirty_ = false;
    rendering_ = false;
}

void frmHospInfoSetting::addLineData(const HospitalRow &data, bool isNew)
{
    const int row = ui->tableInfo->rowCount();
    ui->tableInfo->insertRow(row);
    ui->tableInfo->setRowHeight(row, 40);
    const QStringList values = {data.columnOne, data.columnTwo, data.columnThree};
    for (int column = 0; column < 3; ++column)
    {
        QLineEdit *edit = new QLineEdit(ui->tableInfo);
        edit->setObjectName(QStringLiteral("tableItme%1_%2_Adptive").arg(row).arg(column));
        edit->installEventFilter(this);
        edit->setText(values[column]);
        edit->setFont(ui->tableInfo->font());
        edit->setAlignment(Qt::AlignCenter);
        // Original SystemCommon::getLineEdit() cell style.
        edit->setStyleSheet(QStringLiteral(".QLineEdit { margin: 3px; border: 0px; background: rgba(255,255,255,0); } .QLineEdit:focus { background: rgb(255,255,255); }"));
        ui->tableInfo->setCellWidget(row, column, edit);
    }
    auto *one = qobject_cast<QLineEdit *>(ui->tableInfo->cellWidget(row, 0));
    auto *two = qobject_cast<QLineEdit *>(ui->tableInfo->cellWidget(row, 1));
    auto *three = qobject_cast<QLineEdit *>(ui->tableInfo->cellWidget(row, 2));
    one->setMaxLength(6);
    one->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("^[0-9A-Za-z\\x{4e00}-\\x{9fa5}]{0,6}$")), one));
    one->setToolTip(tr("中文、字母或数字，最多 6 个字符；名称不能为空。"));
    two->setReadOnly(true);
    two->setToolTip(tr("保留原版中文首字母映射；根据名称自动生成。"));
    three->setMaxLength(isDoctorType() ? 6 : 32);
    if (isDoctorType())
    {
        three->setValidator(new QRegularExpressionValidator(
            QRegularExpression(QStringLiteral("^[0-9A-Za-z]{0,6}$")), three));
        three->setToolTip(tr("工号：字母或数字，最多 6 个字符。"));
    }
    else
    {
        three->setToolTip(tr("备注：最多 32 个字符。"));
    }
    ui->tableInfo->setItem(row, 3, new QTableWidgetItem(data.id > 0 ? QString::number(data.id) : QString()));
    ui->tableInfo->setItem(row, 4, new QTableWidgetItem(type_));
    ui->tableInfo->setItem(row, 5, new QTableWidgetItem(isNew ? QStringLiteral("new") : QString()));
    connect(one, &QLineEdit::textChanged, this, [this, two](const QString &text)
    {
        two->setText(Sn100::hospitalMnemonic(text));
        setDirty();
    });
    connect(three, &QLineEdit::textChanged, this, [this]() { setDirty(); });
}

HospitalRows frmHospInfoSetting::collectRows(QString *error) const
{
    HospitalRows rows;
    for (int row = 0; row < ui->tableInfo->rowCount(); ++row)
    {
        HospitalRow data;
        data.id = ui->tableInfo->item(row, 3)->text().toLongLong();
        data.type = type_;
        data.columnOne = qobject_cast<QLineEdit *>(ui->tableInfo->cellWidget(row, 0))->text();
        data.columnTwo = qobject_cast<QLineEdit *>(ui->tableInfo->cellWidget(row, 1))->text();
        data.columnThree = qobject_cast<QLineEdit *>(ui->tableInfo->cellWidget(row, 2))->text();
        if (data.columnOne.isEmpty())
        {
            *error = tr("第 %1 行名称不能为空；尚未保存任何数据。").arg(row + 1);
            return {};
        }
        rows.append(data);
    }
    error->clear();
    return rows;
}

void frmHospInfoSetting::on_btnAdd_clicked()
{
    if (busy_ || !ready_)
    {
        return;
    }
    HospitalRow row;
    row.type = type_;
    addLineData(row, true);
    const int index = ui->tableInfo->rowCount() - 1;
    ui->tableInfo->selectRow(index);
    ui->tableInfo->scrollToBottom();
    ui->tableInfo->cellWidget(index, 0)->setFocus();
    setDirty();
}

void frmHospInfoSetting::on_btnDel_clicked()
{
    if (busy_ || !ready_)
    {
        return;
    }
    const int row = ui->tableInfo->currentRow();
    if (row < 0)
    {
        showStatus(tr("请先选择需要删除的行。"), true);
        return;
    }
    const qint64 id = ui->tableInfo->item(row, 3)->text().toLongLong();
    QString message = tr("是否删除当前选中信息？");
    if (id > 0 && dirty_)
    {
        message += tr("\n删除已保存记录会立即写入数据库，并重读页面；当前其他未保存的更改将被放弃。");
    }
    QMessageBox dialog(QMessageBox::Question, tr("确认删除"), message,
                       QMessageBox::Yes | QMessageBox::No, this);
    dialog.setObjectName(QStringLiteral("deleteRowDialog"));
    dialog.setDefaultButton(QMessageBox::No);
    dialog.setButtonText(QMessageBox::Yes, tr("确认删除"));
    dialog.setButtonText(QMessageBox::No, tr("取消"));
    if (dialog.exec() != QMessageBox::Yes)
    {
        return;
    }
    if (id <= 0)
    {
        ui->tableInfo->removeRow(row);
        setDirty();
        showStatus(tr("已移除尚未保存的新行；数据库未改变。"));
        return;
    }
    pendingOperation_ = QStringLiteral("remove");
    operationWarning_.clear();
    setBusy(true);
    showStatus(tr("正在删除所选记录…"));
    emit requestRemove(type_, id);
}

void frmHospInfoSetting::on_btnSave_clicked()
{
    if (busy_ || !ready_)
    {
        return;
    }
    if (ui->ldtHospName->text().isEmpty())
    {
        showStatus(tr("医院名称不能为空；尚未保存任何数据。"), true);
        return;
    }
    QString error;
    const HospitalRows rows = collectRows(&error);
    if (!error.isEmpty())
    {
        showStatus(error, true);
        return;
    }
    pendingOperation_ = QStringLiteral("save");
    operationWarning_.clear();
    setBusy(true);
    showStatus(tr("正在保存测试数据…"));
    emit requestSave(type_, ui->ldtHospName->text(), rows);
}

void frmHospInfoSetting::on_btnCancel_clicked()
{
    if (!busy_ && ready_ && confirmDiscard(tr("取消更改")))
    {
        beginLoad(QStringLiteral("cancel"), tr("正在取消更改并重新读取…"));
    }
}

void frmHospInfoSetting::on_btnReload_clicked()
{
    if (!busy_ && ready_ && confirmDiscard(tr("刷新 / 重读")))
    {
        beginLoad(QStringLiteral("load"), tr("正在重新读取测试数据…"));
    }
}

bool frmHospInfoSetting::eventFilter(QObject *watched, QEvent *event)
{
    // Embedded line edits consume mouse events before QTableWidget sees them.
    // Resolve the current model index at event time (rows can be removed).
    auto *edit = qobject_cast<QLineEdit *>(watched);
    if (edit && (event->type() == QEvent::FocusIn || event->type() == QEvent::MouseButtonPress))
    {
        const QPoint position = edit->mapTo(ui->tableInfo->viewport(), QPoint(1, 1));
        const QModelIndex index = ui->tableInfo->indexAt(position);
        if (index.isValid())
        {
            ui->tableInfo->setCurrentCell(index.row(), index.column());
        }
    }
    return QWidget::eventFilter(watched, event);
}

void frmHospInfoSetting::closeEvent(QCloseEvent *event)
{
    if (busy_)
    {
        showStatus(tr("数据操作正在进行，请等待结果后再关闭窗口。"));
        event->ignore();
        return;
    }
    if (!confirmDiscard(tr("关闭窗口")))
    {
        event->ignore();
        return;
    }
    QWidget::closeEvent(event);
}
