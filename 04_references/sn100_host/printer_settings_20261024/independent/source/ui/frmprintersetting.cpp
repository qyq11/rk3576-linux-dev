#include "frmprintersetting.h"
#include "ui_frmprintersetting.h"
#include "data/printer_settings_store.h"

#include <QAbstractButton>
#include <QCloseEvent>
#include <QFont>
#include <QMessageBox>
#include <QSignalBlocker>

frmPrinterSetting::frmPrinterSetting(PrinterSettingsStore *store, QWidget *parent) :
    QWidget(parent), ui(new Ui::frmPrinterSetting)
{
    ui->setupUi(this);
    // 原stylecolor.cpp:listWidgetStyle::getStrSheet1及现有有来源的主题键。
    ui->lstProject->setStyleSheet(QStringLiteral(
        "QListWidget::item:selected { background: rgba(183,215,255,255); color: white; }"
        "QListWidget::item { color: #444444; }"
        "QListWidget { font: bold 18px 'Noto Sans CJK SC'; background-color: transparent; }"));
    ui->widget->setStyleSheet(ui->widget->styleSheet() + QStringLiteral(
        ".QPushButton { background-color: rgba(42,118,242,255); color: #FFFFFF; border-radius: 4px; padding: 2px; }"
        ".QPushButton:hover { background-color: rgba(42,118,242,170); }"
        ".QPushButton:pressed { background-color: rgba(135,206,250,255); }"));
    for (QWidget *widget : findChildren<QWidget *>())
    {
        QFont font = widget->font();
        if (font.family().contains(QStringLiteral("苹方")))
        {
            font.setFamily(QStringLiteral("Noto Sans CJK SC"));
            widget->setFont(font);
        }
    }
    connect(ui->lstProject, &QListWidget::currentTextChanged, ui->ldtPrinterSelect, &QLineEdit::setText);
    connect(ui->ldtPrinterSelect, &QLineEdit::textChanged, this, [this]()
    {
        setBusy(busy_);
        if (ready_ && !busy_ && isDirty()) reportStatus(tr("打印设备选择尚未应用。"));
    });
    connect(this, &frmPrinterSetting::requestSave, store, &PrinterSettingsStore::save, Qt::QueuedConnection);
    connect(store, &PrinterSettingsStore::loaded, this, [this](const QString &name, const QStringList &available)
    {
        render(name, available);
        const QString operation = operation_;
        operation_.clear();
        emit operationFinished(operation.isEmpty() ? QStringLiteral("load") : operation, true);
    });
    connect(store, &PrinterSettingsStore::saved, this, [this](const QString &name, const QStringList &available)
    {
        render(name, available);
        operation_.clear();
        reportStatus(tr("打印机本地设置已保存；未执行打印或更改系统默认设备。"));
        emit operationFinished(QStringLiteral("save"), true);
    });
    connect(store, &PrinterSettingsStore::error, this, [this](const QString &operation, const QString &message)
    {
        if (operation == QStringLiteral("initialize")) ready_ = false;
        operation_.clear();
        setBusy(false);
        reportStatus(message, true);
        emit operationFinished(operation, false);
    });
    setBusy(true);
}

frmPrinterSetting::~frmPrinterSetting()
{
    delete ui;
}

bool frmPrinterSetting::isDirty() const
{
    return ready_ && ui->ldtPrinterSelect->text() != committed_;
}

void frmPrinterSetting::setBusy(bool busy)
{
    const bool changed = busy_ != busy;
    busy_ = busy;
    ui->lstProject->setEnabled(ready_ && !busy_);
    ui->btnApply->setEnabled(ready_ && !busy_ && isDirty());
    if (changed) emit busyChanged(busy_);
}

void frmPrinterSetting::reportStatus(const QString &message, bool error)
{
    status_ = message;
    setToolTip(message);
    emit statusChanged(message, error);
}

void frmPrinterSetting::render(const QString &name, const QStringList &available)
{
    const QSignalBlocker listBlocker(ui->lstProject);
    ui->lstProject->clear();
    for (const QString &printer : available)
    {
        auto *item = new QListWidgetItem(printer, ui->lstProject);
        item->setSizeHint(QSize(ui->lstProject->width() - 5, 40));
    }
    committed_ = name;
    ui->ldtPrinterSelect->setText(name);
    if (available.contains(name)) ui->lstProject->setCurrentRow(available.indexOf(name));
    ready_ = true;
    setBusy(false);
    reportStatus(name.isEmpty() ? tr("尚未选择打印设备；没有设备时可保留空列表。")
        : available.contains(name) ? tr("已读取本地打印设备设置。")
        : tr("已保存的设备当前不可用，原名称保留；请选择本机可用设备后应用。"));
}

void frmPrinterSetting::on_btnApply_clicked()
{
    if (!ready_ || busy_ || !isDirty()) return;
    operation_ = QStringLiteral("save");
    setBusy(true);
    emit requestSave(ui->ldtPrinterSelect->text());
}

bool frmPrinterSetting::requestLeave(const QString &action)
{
    if (busy_)
    {
        reportStatus(tr("打印设置正在处理，请等待完成后再%1。").arg(action), true);
        return false;
    }
    if (!isDirty()) return true;
    // 原页没有取消按钮；沿用统一程序的离页安全补全，不重画业务区域。
    QMessageBox dialog(QMessageBox::Question, tr("确认放弃未保存的更改"),
        tr("%1将放弃打印设备选择，是否继续？").arg(action), QMessageBox::Yes | QMessageBox::No, this);
    dialog.setObjectName(QStringLiteral("discardPrinterChangesDialog"));
    dialog.setDefaultButton(QMessageBox::No);
    dialog.setButtonText(QMessageBox::Yes, tr("放弃并继续"));
    dialog.setButtonText(QMessageBox::No, tr("保留更改"));
    for (QAbstractButton *button : dialog.buttons())
        button->setMinimumWidth(button->fontMetrics().horizontalAdvance(button->text()) + 48);
    if (dialog.exec() != QMessageBox::Yes) return false;
    const QSignalBlocker blocker(ui->lstProject);
    ui->lstProject->setCurrentRow(-1);
    ui->ldtPrinterSelect->setText(committed_);
    setBusy(false);
    reportStatus(tr("已放弃未保存的打印设备选择。"));
    return true;
}

void frmPrinterSetting::closeEvent(QCloseEvent *event)
{
    if (requestLeave(tr("关闭窗口"))) event->accept();
    else event->ignore();
}
