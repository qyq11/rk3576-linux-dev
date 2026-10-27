#ifndef SN100_MASKDLGLOGIN_H
#define SN100_MASKDLGLOGIN_H

#include <QDialog>
#include <QPointer>

class AccountStore;
class CusMessageBox;
class QCloseEvent;
class QKeyEvent;

namespace Ui
{
class MaskDlgLogin;
}

// 保留原登录卡；SQL认证由所属工作线程中的AccountStore完成。
class MaskDlgLogin : public QDialog
{
    Q_OBJECT

public:
    explicit MaskDlgLogin(AccountStore *store, QWidget *parent = nullptr);
    ~MaskDlgLogin() override;
    bool isBusy() const;

public slots:
    void showDialog();
    void reject() override;

signals:
    void busyChanged(bool busy);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void submitLogin();

private:
    Ui::MaskDlgLogin *ui;
    QPointer<AccountStore> store_;
    QPointer<CusMessageBox> message_;
    quint64 requestId_ = 0;
    bool loadingDefault_ = false;
    bool busy_ = false;
    bool shown_ = false;
    bool closing_ = false;
    bool finishing_ = false;
    int result_ = QDialog::Rejected;

    void setBusy(bool busy);
    void finishAnimated(int result);
    void showMessage(const QString &message, bool acceptAfterAcknowledged);
};

#endif
