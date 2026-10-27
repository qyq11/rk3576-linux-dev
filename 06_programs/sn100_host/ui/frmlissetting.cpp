#include "frmlissetting.h"
#include "ui_frmlissetting.h"

#include <QAbstractButton>
#include <QCloseEvent>
#include <QFont>
#include <QIntValidator>
#include <QListView>
#include <QMessageBox>

frmLisSetting::frmLisSetting(LisSettingsStore *store, QWidget *parent) :
    QWidget(parent), ui(new Ui::frmLisSetting)
{
    ui->setupUi(this);
    // 原radio/check文字、颜色及重叠分组不变；不导入会自动连接的LisCommSetting。
    ui->radioBoxTcp->setLabelPosition(QtMaterialCheckable::LabelPositionRight);
    ui->radioBoxCom->setLabelPosition(QtMaterialCheckable::LabelPositionRight);
    ui->radioBoxTcp->setText(QStringLiteral("Tcp"));
    ui->radioBoxCom->setText(tr("串口"));
    for (auto *radio : {ui->radioBoxTcp, ui->radioBoxCom})
    {
        radio->setCheckedColor(Qt::black);
        radio->setUncheckedColor(Qt::black);
    }
    ui->ckxAutoGet->setText(tr("自动获取"));
    ui->ckxAutoSend->setText(tr("自动上传"));
    ui->ckxAutoLink->setText(tr("应答模式"));
    ui->cbxCom->setView(new QListView());
    ui->cbxBaudrate->setView(new QListView());
    const QString comboStyle = QStringLiteral(
        "QComboBox { border: 1px solid #E4E8EB; border-radius: 5px; padding: 2px 4px;"
        " background-color: transparent; color: #444444; font: 18px 'Noto Sans CJK SC'; }"
        "QComboBox:disabled { background-color: #E4E4E4; }"
        "QComboBox QAbstractItemView { border: none; color: #444444; font: 20px 'Noto Sans CJK SC'; }"
        "QComboBox QAbstractItemView::item { height: 50px; }"
        "QComboBox QAbstractItemView::item:selected { background: rgba(183,215,255,255); color: white; }"
        "QComboBox::drop-down { width: 20px; border: none; }"
        "QComboBox::down-arrow { width: 25px; height: 25px; image: url(:/image/arrow_drop_down.svg); }");
    ui->cbxCom->setStyleSheet(comboStyle);
    ui->cbxBaudrate->setStyleSheet(comboStyle);
    ui->lblTcpIp->setMaxLength(16);
    ui->lblTcpPort->setMaxLength(5);
    ui->lblTcpPort->setValidator(new QIntValidator(0, 99999, this));
    ui->lineEdit_27->setStyleSheet(QStringLiteral("border:none; background-color:transparent; border-left:10px solid rgba(42,118,242,255);"));
    ui->widget_5->setStyleSheet(ui->widget_5->styleSheet() + QStringLiteral(
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
    // 切模式只改变可见性；不重新读取已提交值覆盖另一模式的草稿。
    connect(ui->radioBoxTcp, &QAbstractButton::toggled, this, [this](bool checked)
    {
        if (!rendering_ && checked) { draft_.type = QStringLiteral("tcp"); showMode(); editDraft(); }
    });
    connect(ui->radioBoxCom, &QAbstractButton::toggled, this, [this](bool checked)
    {
        if (!rendering_ && checked) { draft_.type = QStringLiteral("serial"); showMode(); editDraft(); }
    });
    for (QLineEdit *edit : {ui->lblTcpIp, ui->lblTcpPort})
        connect(edit, &QLineEdit::textChanged, this, [this]() { editDraft(); });
    for (QComboBox *combo : {ui->cbxCom, ui->cbxBaudrate, ui->cmb_SamLis_Mode})
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { editDraft(); });
    for (CusCheckBox *check : {ui->ckxAutoGet, ui->ckxAutoSend, ui->ckxAutoLink})
        connect(check, QOverload<bool>::of(&CusCheckBox::signalClicked), this, [this]() { editDraft(); });
    connect(this, &frmLisSetting::requestSave, store, &LisSettingsStore::save, Qt::QueuedConnection);
    connect(store, &LisSettingsStore::loaded, this, [this](LisSettings settings, QStringList ports)
    {
        const bool first = !ready_;
        render(settings, ports);
        emit operationFinished(first ? QStringLiteral("initialize") : QStringLiteral("load"), true);
    });
    connect(store, &LisSettingsStore::saved, this, [this](LisSettings settings, QStringList ports)
    {
        render(settings, ports);
        reportStatus(tr("LIS本地设置已保存；未连接、未打开串口，自动选项仅保存意图。"));
        emit operationFinished(QStringLiteral("save"), true);
    });
    connect(store, &LisSettingsStore::error, this, [this](const QString &operation, const QString &message)
    {
        if (operation == QStringLiteral("initialize")) ready_ = false;
        setBusy(false);
        reportStatus(message, true);
        emit operationFinished(operation, false);
    });
    setBusy(true);
}

frmLisSetting::~frmLisSetting()
{
    delete ui;
}

void frmLisSetting::setBusy(bool busy)
{
    const bool changed = busy_ != busy;
    busy_ = busy;
    ui->widget_5->setEnabled(ready_ && !busy_);
    ui->btnSave->setEnabled(ready_ && !busy_ && isDirty());
    if (changed) emit busyChanged(busy_);
}

void frmLisSetting::reportStatus(const QString &message, bool error)
{
    status_ = message;
    setToolTip(message);
    emit statusChanged(message, error);
}

void frmLisSetting::showMode()
{
    const bool tcp = draft_.type == QStringLiteral("tcp");
    ui->lblInfo1->setText(tcp ? tr("主计算机ip地址:") : tr("主计算机串口名称:"));
    ui->lblInfo2->setText(tcp ? tr("主计算机端口号:") : tr("主计算机波特率:"));
    ui->lblTcpIp->setVisible(tcp);
    ui->lblTcpPort->setVisible(tcp);
    ui->cbxCom->setVisible(!tcp);
    ui->cbxBaudrate->setVisible(!tcp);
}

void frmLisSetting::render(LisSettings settings, QStringList ports)
{
    rendering_ = true;
    committed_ = draft_ = settings;
    available_ = ports;
    ui->lblTcpIp->setText(settings.ip);
    ui->lblTcpPort->setText(settings.port);
    ui->cbxCom->clear();
    // 空项是未配置，不因无可用端口或旧端口缺失而落到首个设备。
    ui->cbxCom->addItem(QString());
    ui->cbxCom->addItems(ports);
    if (!settings.commName.isEmpty() && !ports.contains(settings.commName))
    {
        ui->cbxCom->addItem(settings.commName);
        ui->cbxCom->setItemData(ui->cbxCom->count() - 1, tr("已保存但当前未枚举到的端口；仅保留名称，不连接。"), Qt::ToolTipRole);
    }
    ui->cbxCom->setCurrentText(settings.commName);
    ui->cbxCom->setToolTip(!settings.commName.isEmpty() && !ports.contains(settings.commName)
        ? tr("已保存端口当前不可用，原名保留；未打开设备。") : tr("仅本机元数据候选名，未打开或验证端口。"));
    ui->cbxBaudrate->clear();
    ui->cbxBaudrate->addItem(QString());
    ui->cbxBaudrate->addItems(LisSettingsStore::supportedBaudrates());
    if (!settings.baudrate.isEmpty() && ui->cbxBaudrate->findText(settings.baudrate) < 0) ui->cbxBaudrate->addItem(settings.baudrate);
    ui->cbxBaudrate->setCurrentText(settings.baudrate);
    ui->cmb_SamLis_Mode->setCurrentIndex(settings.sampleMode);
    ui->ckxAutoGet->setCheckState(settings.autoGet ? Qt::Checked : Qt::Unchecked);
    ui->ckxAutoSend->setCheckState(settings.autoSend ? Qt::Checked : Qt::Unchecked);
    ui->ckxAutoLink->setCheckState(settings.link ? Qt::Checked : Qt::Unchecked);
    ui->radioBoxTcp->setChecked(settings.type == QStringLiteral("tcp"));
    ui->radioBoxCom->setChecked(settings.type != QStringLiteral("tcp"));
    showMode();
    rendering_ = false;
    ready_ = true;
    setBusy(false);
    reportStatus(tr("已读取LIS本地意图；未确认端点/串口可保留空值，未建立连接。"));
}

void frmLisSetting::editDraft()
{
    if (rendering_ || !ready_ || busy_) return;
    draft_.ip = ui->lblTcpIp->text();
    draft_.port = ui->lblTcpPort->text();
    draft_.commName = ui->cbxCom->currentText();
    draft_.baudrate = ui->cbxBaudrate->currentText();
    draft_.sampleMode = ui->cmb_SamLis_Mode->currentIndex();
    draft_.autoGet = ui->ckxAutoGet->isChecked();
    draft_.autoSend = ui->ckxAutoSend->isChecked();
    draft_.link = ui->ckxAutoLink->isChecked();
    setBusy(false);
    if (isDirty()) reportStatus(tr("LIS设置尚未保存；保存包括两种模式的本地草稿，不执行连接。"));
}

void frmLisSetting::on_btnSave_clicked()
{
    if (!ready_ || busy_ || !isDirty()) return;
    const QString error = LisSettingsStore::validate(draft_);
    if (!error.isEmpty())
    {
        reportStatus(error, true);
        emit operationFinished(QStringLiteral("validate"), false);
        return;
    }
    if (draft_.type.isEmpty()) draft_.type = QStringLiteral("serial");
    setBusy(true);
    emit requestSave(draft_);
}

bool frmLisSetting::requestLeave(const QString &action)
{
    if (busy_)
    {
        reportStatus(tr("LIS设置正在处理，请等待完成后再%1。").arg(action), true);
        return false;
    }
    if (!isDirty()) return true;
    QMessageBox dialog(QMessageBox::Question, tr("确认放弃未保存的更改"),
        tr("%1将放弃两种通讯模式的LIS草稿，是否继续？").arg(action), QMessageBox::Yes | QMessageBox::No, this);
    dialog.setObjectName(QStringLiteral("discardLisChangesDialog"));
    dialog.setDefaultButton(QMessageBox::No);
    dialog.setButtonText(QMessageBox::Yes, tr("放弃并继续"));
    dialog.setButtonText(QMessageBox::No, tr("保留更改"));
    for (QAbstractButton *button : dialog.buttons())
        button->setMinimumWidth(button->fontMetrics().horizontalAdvance(button->text()) + 48);
    if (dialog.exec() != QMessageBox::Yes) return false;
    render(committed_, available_);
    return true;
}

void frmLisSetting::closeEvent(QCloseEvent *event)
{
    if (requestLeave(tr("关闭窗口"))) event->accept();
    else event->ignore();
}
