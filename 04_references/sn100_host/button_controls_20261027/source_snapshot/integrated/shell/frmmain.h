#ifndef SN100_SHELL_FRMMAIN_H
#define SN100_SHELL_FRMMAIN_H

#include <QWidget>
#include <QPointer>
#include <QMap>
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
class ProjectPage;
class ReagentsPage;
class SamplePage;
class InstrumentPage;
class MaskDlgLogin;
class MaskDlgShutDown;
class DeviceControl;
class frmPrinterSetting;
class frmLisSetting;
class QualityControlPage;
class CalibrationPage;
class LanguageSettingPage;
class AlarmHistoryHost;
class LisInboxPageHost;
class QAction;
namespace AlarmHistory { class Store; }
namespace Sn100Protocol { struct ReplayState; struct LoopbackStatusState; }

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
    void attachLanguagePage(LanguageSettingPage *page);
    LanguageSettingPage *languagePage() const;
    bool enableAlarmHistory(AlarmHistory::Store *store, const QString &absolutePath);
    void setAlarmHistoryAvailable(bool available);
    AlarmHistoryHost *alarmHistoryHost() const { return alarmHistory_; }
    void attachPrinterPage(frmPrinterSetting *page);
    frmPrinterSetting *printerPage() const;
    void attachLisPage(frmLisSetting *page);
    frmLisSetting *lisPage() const;
    bool enableLisRawInbox(const QString &absoluteDirectory);
    void openLisRawInbox();
    bool lisRawInboxShutdownComplete() const;
    void attachQualityControlPage(QualityControlPage *page);
    QualityControlPage *qualityControlPage() const { return qualityControl_; }
    void attachCalibrationPage(CalibrationPage *page);
    CalibrationPage *calibrationPage() const { return calibration_; }
    // 页面归主壳；HistoryStore和工作线程仍归入口管理。
    void attachHistoryPage(HistoryPage *page);
    HistoryPage *historyPage() const;
    void attachCatalogPages(ProjectPage *projects, ReagentsPage *reagents);
    ProjectPage *projectPage() const { return projects_; }
    ReagentsPage *reagentPage() const { return reagents_; }
    void attachSamplePage(SamplePage *samples);
    SamplePage *samplePage() const { return samples_; }
    void attachInstrumentPage(InstrumentPage *page);
    InstrumentPage *instrumentPage() const { return instrument_; }
    // Requires an attached history page; all report windows belong to this shell.
    bool enableHistoryReports(const QString &directory);
    HistoryReportController *historyReports() const { return reports_; }
    AccountSession accountSession() const { return session_; }
    bool enablePasswordLogin();
    bool requestLogin();
    bool requestLogout();
    bool requestShutdown();
    bool enableDeviceControl(bool connectAtStartup = true);
    DeviceControl *deviceControl() const { return deviceControl_; }
    int currentMainPage() const;
    int currentUserPage() const;
    bool selectMainPage(int index);
    bool selectUserPage(int index);
    void setDataDirectory(const QString &directory);
    // 只呈现显式离线回放；不改变账户动作条件或启用任何设备按钮。
    void setStatusReplayState(const Sn100Protocol::ReplayState &state);
    // 自建本机peer的只读观测；与文件回放使用不同来源提示，不解禁设备动作。
    void setLoopbackStatusState(const Sn100Protocol::LoopbackStatusState &state);

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
    bool passwordLoginEnabled_ = false;
    QPointer<MaskDlgLogin> loginDialog_;
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
    ProjectPage *projects_ = nullptr;
    ReagentsPage *reagents_ = nullptr;
    SamplePage *samples_ = nullptr;
    InstrumentPage *instrument_ = nullptr;
    QualityControlPage *qualityControl_ = nullptr;
    QScrollArea *qualityViewport_ = nullptr;
    QPointer<QWidget> lastQualityFocus_;
    QString qualityStatus_;
    bool qualityStatusError_ = false;
    CalibrationPage *calibration_ = nullptr;
    QScrollArea *calibrationViewport_ = nullptr;
    QPointer<QWidget> lastCalibrationFocus_;
    QString calibrationStatus_;
    bool calibrationStatusError_ = false;
    QString instrumentStatus_;
    bool instrumentStatusError_ = false;
    QString lisStatus_;
    bool lisStatusError_ = false;
    QPointer<LisInboxPageHost> lisRawInbox_;
    QPointer<QAction> lisRawInboxAction_;
    QString lisRawInboxDirectory_;
    bool lisRawInboxShutdownPending_ = false;
    bool lisRawInboxCloseBlocked_ = false;
    QString printerStatus_;
    bool printerStatusError_ = false;
    QScrollArea *projectViewport_ = nullptr;
    QScrollArea *reagentViewport_ = nullptr;
    QScrollArea *sampleViewport_ = nullptr;
    QPointer<QWidget> lastProjectFocus_;
    QPointer<QWidget> lastReagentFocus_;
    QPointer<QWidget> lastSampleFocus_;
    QString projectStatus_, reagentStatus_;
    QString sampleStatus_;
    bool projectStatusError_ = false;
    bool reagentStatusError_ = false;
    bool sampleStatusError_ = false;
    AlarmHistoryHost *alarmHistory_ = nullptr;
    QString alarmHistoryPath_;
    bool alarmHistoryAvailable_ = false;
    bool alarmHistoryShutdownPending_ = false;
    bool alarmHistoryShutdownRetryQueued_ = false;
    bool readOnlyStatusEnabled_ = false;
    QString readOnlyStatusText_, readOnlyStatusDetail_, readOnlyStatusColor_;
    QPointer<MaskDlgShutDown> shutdownDialog_;
    bool shutdownClosing_ = false;
    DeviceControl *deviceControl_ = nullptr;
    bool connectDeviceAtStartup_ = false;
    void updateDeviceControlActions();
    QString detectionStartBlocker() const;
    void attachDeviceTaskStore();
    bool deviceTaskStoreAttached_ = false, deviceTaskPreparing_ = false;
    quint64 deviceTaskRequest_ = 0, deviceTaskConnection_ = 0, devicePacket_ = 0;
    QMap<quint64, qint64> devicePackets_;
    qint64 devicePacketBytes_ = 0;

    void restoreMainSelection();
    void configureOfflineStatus();
    void updateAccountStatus();
    void applyAccountSession(const AccountSession &session);
    bool businessPagesIdle() const;
    bool alarmHistoryIdle() const;
    bool requestAlarmHistoryLeave(const QString &action);
    bool rejectLoginNavigation();
    bool requestHistoryLeave(const QString &action);
    void restoreHistoryFocus();
    void updateHistoryStatus();
    void updateHospitalStatus();
    void updateReportHospitalName(const QString &name);
    void reportStatus(const QString &message, bool error = false);
    bool catalogPagesIdle() const;
    bool requestCatalogLeave(const QString &action, bool currentOnly = false);
    void restoreCatalogFocus();
    void updateCatalogStatus();
    void updateReadOnlyStatusDisplay();
    bool requestInstrumentLeave(const QString &action);
    void updateInstrumentStatus();
    void updatePrinterStatus();
    void updateLisStatus();
    bool requestLisRawInboxShutdown();
    bool qualityPagesIdle() const;
    bool requestQualityLeave(const QString &action, bool currentOnly = false);
    void updateQualityStatus();
    void restoreQualityFocus();
    bool calibrationPageIdle() const;
    bool requestCalibrationLeave(const QString &action, bool currentOnly = false);
    void updateCalibrationStatus();
    void restoreCalibrationFocus();
};

#endif
