#ifndef SN100_FRMACCOUNTSETTING_H
#define SN100_FRMACCOUNTSETTING_H

#include <QWidget>
#include "data/account_store.h"

class QCloseEvent;
class QComboBox;
class QResizeEvent;

namespace Ui
{
class frmAccountSetting;
}

// Original account page, connected only to the anonymous offline account store.
// The host supplies a simulated session and device-ready state to that worker.
class frmAccountSetting : public QWidget
{
    Q_OBJECT

public:
    explicit frmAccountSetting(AccountStore *store, QWidget *parent = nullptr);
    ~frmAccountSetting() override;

    bool isReady() const { return ready_; }
    bool isBusy() const { return busy_; }
    QString statusText() const { return status_; }
    // The shell keeps the original business positions instead of the stand-alone
    // host's horizontal centering. Neither mode changes account rules.
    void setEmbeddedInShell();
    bool requestLeave(const QString &action);
    // Clears UI identity/edit caches only. AccountStore remains the sole session
    // source; this must not cancel or disguise an in-flight committed operation.
    void resetForLogout();

signals:
    void requestLoad();
    void requestAdd(QString name, QString password, QString again, int role);
    void requestRemove(QString name, QString password);
    void requestUpdate(QString oldName, QString oldPassword, QString newName,
                       QString password, QString again, int role);
    void signalLoginOut();
    void statusChanged(QString message, bool error);
    void operationFinished(QString operation, bool success);
    void busyChanged(bool busy);

public slots:
    void slotUserChanged();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void on_btnMenuAdd_clicked();
    void on_btnMenuDel_clicked();
    void on_btnMenuChange_clicked();
    void on_btnAdd_clicked();
    void on_btnDel_clicked();
    void on_btnUpdate_clicked();
    void slotSetText(int row, int column);

private:
    Ui::frmAccountSetting *ui;
    AccountRows displayedRows_;
    AccountSession session_;
    QString status_;
    QString pendingOperation_;
    QString successMessage_;
    bool ready_ = false;
    bool busy_ = true;
    bool awaitingLoad_ = false;
    bool embeddedInShell_ = false;

    void setBusy(bool busy);
    void reportStatus(const QString &message, bool error = false);
    void rejectOperation(const QString &operation, const QString &message);
    void beginRead(const QString &operation);
    void frmGetAccount(const AccountRows &rows);
    void refreshRoleOptions();
    void clearPasswords();
    int selectedRole(const QComboBox *combo) const;
};

#endif
