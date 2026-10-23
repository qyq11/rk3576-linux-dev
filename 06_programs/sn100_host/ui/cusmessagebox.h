#ifndef SN100_CUSMESSAGEBOX_H
#define SN100_CUSMESSAGEBOX_H

#include <QDialog>
#include <QMessageBox>

namespace Ui
{
class CusMessageBox;
}

// Original message view and masks, isolated from the full application. Only
// the four dialog types used by the account slice are extracted here.
class CusMessageBox : public QDialog
{
    Q_OBJECT

public:
    explicit CusMessageBox(QWidget *parent = nullptr);
    ~CusMessageBox() override;

    void showInformationBox(QString message);
    void showQuestionBox(QString message);
    void showWarningBox(QString message);
    void showErrorBox(QString message);
    QMessageBox::StandardButton getClickButton() const;
    QString getMsg() const;
    bool isReadyForInput() const;
    void closeDialog();

public slots:
    void reject() override;

private slots:
    void on_btnClose_clicked();
    void on_btnSure_clicked();
    void on_btnCancel_clicked();

private:
    Ui::CusMessageBox *ui;
    QMessageBox::StandardButton clickResult_ = QMessageBox::NoButton;

    void showDialog(const QString &message, const QString &icon,
                    const QString &buttonPrefix, bool question);
};

#endif
