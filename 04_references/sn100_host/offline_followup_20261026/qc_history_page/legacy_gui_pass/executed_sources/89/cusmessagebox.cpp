// Extracted from reference customwidgets/cusmessagebox.cpp. The original PNGs
// provide all four semantic icon/button colors. Resource paths become QRC;
// view_frame geometry, labels and font hierarchy come from the original .ui.
#include "cusmessagebox.h"
#include "ui_cusmessagebox.h"

CusMessageBox::CusMessageBox(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::CusMessageBox)
{
    ui->setupUi(this);
    // Original inherited buttonStyle contributes radius and padding. The
    // semantic PNGs replace its background on the actual message buttons.
    setStyleSheet(QStringLiteral("QPushButton { border-radius: 4px; padding: 2px; }"));
    // Preserve the Material close button's transparent, flat appearance and
    // original 13x13 icon; only its ripple effect is outside this extraction.
    ui->btnClose->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; border: none; border-radius: 0px; padding: 0px; }"));
    ui->lblInfo->setTextFormat(Qt::PlainText);
    ui->mask->setupForm();
    connect(ui->mask, &CusDialogMask::closeFinished, this, [this]()
    {
        // Original parentWidget->close() returns a rejected modal result; the
        // public getClickButton() is the authoritative Yes/No response.
        QDialog::done(QDialog::Rejected);
    });
}

CusMessageBox::~CusMessageBox()
{
    delete ui;
}

void CusMessageBox::prepareDialog(const QString &message, const QString &icon,
                               const QString &buttonPrefix, bool question)
{
    clickResult_ = QMessageBox::NoButton;
    ui->lblInfo->setText(message);
    ui->lblIco->setStyleSheet(QStringLiteral("QLabel { border-image: url(:/messagebox/%1); }").arg(icon));
    const QString buttonStyle = QStringLiteral(
        ".QPushButton { color: #FFFFFF; border-image: url(:/messagebox/%1_normal.png); }"
        ".QPushButton:hover { color: #FFFFFF; border-image: url(:/messagebox/%1_hover.png); }"
        ".QPushButton:pressed { color: #FFFFFF; border-image: url(:/messagebox/%1_pressed.png); }")
        .arg(buttonPrefix);
    ui->btnSure->setStyleSheet(buttonStyle);
    ui->btnCancel->setStyleSheet(question ? buttonStyle : QString());
    ui->btnSure->show();
    ui->btnCancel->setVisible(question);
    ui->mask->showAnimation();
}

void CusMessageBox::showDialog(const QString &message, const QString &icon,
                               const QString &buttonPrefix, bool question)
{
    prepareDialog(message, icon, buttonPrefix, question);
    exec();
}

void CusMessageBox::openWarningBox(QString message)
{
    prepareDialog(message, QStringLiteral("message_warning.png"), QStringLiteral("message_warn"), false);
    open();
}

void CusMessageBox::showInformationBox(QString message)
{
    showDialog(message, QStringLiteral("message_info.png"), QStringLiteral("message_info"), false);
}

void CusMessageBox::showQuestionBox(QString message)
{
    showDialog(message, QStringLiteral("message_ques.png"), QStringLiteral("message_ques"), true);
}

void CusMessageBox::showWarningBox(QString message)
{
    showDialog(message, QStringLiteral("message_warning.png"), QStringLiteral("message_warn"), false);
}

void CusMessageBox::showErrorBox(QString message)
{
    showDialog(message, QStringLiteral("message_error.png"), QStringLiteral("message_error"), false);
}

QMessageBox::StandardButton CusMessageBox::getClickButton() const
{
    return clickResult_;
}

QString CusMessageBox::getMsg() const
{
    return ui->lblInfo->text();
}

bool CusMessageBox::isReadyForInput() const
{
    return ui->mask->isReadyForInput();
}

void CusMessageBox::closeDialog()
{
    ui->mask->closeAnimation();
}

void CusMessageBox::reject()
{
    closeDialog();
}

void CusMessageBox::on_btnClose_clicked()
{
    closeDialog();
}

void CusMessageBox::on_btnSure_clicked()
{
    if (isReadyForInput())
    {
        clickResult_ = QMessageBox::Yes;
        closeDialog();
    }
}

void CusMessageBox::on_btnCancel_clicked()
{
    if (isReadyForInput())
    {
        clickResult_ = QMessageBox::No;
        closeDialog();
    }
}
