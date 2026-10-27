#include "maskdlgshutdown.h"
#include "ui_maskdlgshutdown.h"
#include <QCloseEvent>

MaskDlgShutDown::MaskDlgShutDown(QWidget *parent)
    : QDialog(parent), ui(new Ui::MaskDlgShutDown)
{
    ui->setupUi(this);
    setObjectName(QStringLiteral("MaskDlgShutDown"));
    ui->mask->setupForm();
    // 沿用已移植原登录卡的三态按钮主题；实际Windows活动主题仍须单独对照。
    ui->btnShutDown->setStyleSheet(QStringLiteral(
        ".QPushButton { background-color: rgba(42,118,242,255); color: #FFFFFF;"
        " border-radius: 4px; padding: 2px; }"
        ".QPushButton:hover { background-color: rgba(42,118,242,170); }"
        ".QPushButton:pressed { background-color: rgba(135,206,250,255); }"));
    ui->ckxClose->setText(tr("关闭仪器"));
    ui->ckxClose->hide();
    ui->lblHoleinfo_7->hide();
    ui->btnShutDown->setAccessibleName(tr("确认退出上位机"));
    ui->btnShutDown->setToolTip(tr("退出上位机软件。"));
    connect(ui->btnShutDown, &QPushButton::clicked, this,
            [this]() { finishAnimated(QDialog::Accepted); });
    connect(ui->btnClose, &QPushButton::clicked, this, &MaskDlgShutDown::reject);
    connect(ui->mask, &CusDialogMask::closeFinished, this, [this]()
    {
        finishing_ = true;
        QDialog::done(result_);
    });
}

MaskDlgShutDown::~MaskDlgShutDown()
{
    delete ui;
}

void MaskDlgShutDown::showDialog()
{
    open();
    ui->mask->showAnimation();
}

bool MaskDlgShutDown::isReadyForInput() const
{
    return !closing_ && ui->mask->isReadyForInput();
}

void MaskDlgShutDown::finishAnimated(int result)
{
    if (!isReadyForInput()) return;
    closing_ = true;
    result_ = result;
    ui->btnShutDown->setEnabled(false);
    ui->btnClose->setEnabled(false);
    ui->mask->closeAnimation();
}

void MaskDlgShutDown::reject()
{
    finishAnimated(QDialog::Rejected);
}

void MaskDlgShutDown::closeEvent(QCloseEvent *event)
{
    if (finishing_) event->accept();
    else
    {
        event->ignore();
        reject();
    }
}
