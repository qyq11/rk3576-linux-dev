#ifndef SN100_SHELL_FRMMAIN_H
#define SN100_SHELL_FRMMAIN_H

#include <QWidget>
#include <QPointer>
#include "data/account_store.h"
#include "history/report_document.h"

class HospitalStore;
class frmHospInfoSetting;
class frmAccountSetting;
class frmUserSet;
class QButtonGroup;
class QCloseEvent;
class HistoryPage;
class QScrollArea;
class HistoryReportController;

namespace Ui
{
class frmMain;
}

// 脱机主框架。页面和报告控制器归本窗，存储及工作线程归入口。
class frmMain : public QWidget
{
    Q_OBJECT

public:
    explicit frmMain(HospitalStore *store, QWidget *parent = nullptr,
                     AccountStore *accounts = nullptr);
    ~frmMain() override;

    frmHospInfoSetting *hospitalPage() const;
    frmAccountSetting *accountPage() const;
    // 页面归主壳；HistoryStore和工作线程仍归入口管理。
    void attachHistoryPage(HistoryPage *page);
    HistoryPage *historyPage() const;
    // Requires an attached history page; all report windows belong to this shell.
    bool enableHistoryReports(const QString &directory);
    HistoryReportController *historyReports() const { return reports_; }
    AccountSession accountSession() const { return session_; }
    bool requestLogout();
    int currentMainPage() const;
    int currentUserPage() const;
    bool selectMainPage(int index);
    bool selectUserPage(int index);
    void setDataDirectory(const QString &directory);

signals:
    void navigationChanged(int mainIndex, int userIndex);
    void navigationRejected(QString reason);
    void sessionChanged(AccountSession session);
    void loggedOut();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    Ui::frmMain *ui;
    frmUserSet *userSet_;
    QButtonGroup *menuButtons_;
    QPointer<AccountStore> accounts_;
    AccountSession session_;
    bool logoutPending_ = false;
    HistoryPage *history_ = nullptr;
    QScrollArea *historyViewport_ = nullptr;
    QPointer<QWidget> lastHistoryFocus_;
    QString historyStatus_;
    bool historyStatusError_ = false;
    QString hospitalStatus_;
    bool hospitalStatusError_ = false;
    HistoryReportController *reports_ = nullptr;
    HistoryPrintOptions reportOptions_;
    QString committedHospitalName_;
    bool reportHasOutcome_ = false;

    void restoreMainSelection();
    void configureOfflineStatus();
    void updateAccountStatus();
    void applyAccountSession(const AccountSession &session);
    bool requestHistoryLeave(const QString &action);
    void restoreHistoryFocus();
    void updateHistoryStatus();
    void updateHospitalStatus();
    void updateReportHospitalName(const QString &name);
    void reportStatus(const QString &message, bool error = false);
};

#endif
