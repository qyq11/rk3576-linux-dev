#ifndef SN100_SHELL_FRMMAIN_H
#define SN100_SHELL_FRMMAIN_H

#include <QWidget>
#include "reagents/original_reagent_probe.h"
#include <QPointer>
#include <QMap>
#include <QSet>
#include <QElapsedTimer>
#include <memory>
#include "data/account_store.h"
#include "history/report_document.h"
#include "original_files/original_runtime_settings.h"
#include "projects/original_project_save_notice.h"
#include "lis_transport/lis_connection_values.h"

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
namespace OriginalLevels { struct ReceivedPacket; class ObservationWorker; struct Context; }
namespace OriginalTaskReagents { struct Commit; struct RuntimeAdoption; struct RuntimeBasis; }
namespace TaskReagentRouting { struct Bridge; }
class SamplePage;
class SampleStore;
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
namespace AlarmHistory { class Store; class OriginalAlarmReceiver; class OriginalCurrentAlarmState; }
namespace Sn100Protocol { struct ReplayState; struct LoopbackStatusState; }
namespace OriginalRuntime { struct RuntimeSettings; struct SystemProfile; }

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
    void setLisConnectionState(Sn100Lis::ConnectionState state);
    bool enableLisRawInbox(const QString &absoluteDirectory);
    void openLisRawInbox();
    bool lisRawInboxShutdownComplete() const;
    bool enableOriginalAlarmReceiver(AlarmHistory::OriginalAlarmReceiver *receiver);
    // 只挂当前观察；等待毫秒数是本地提示门槛，不是设备回执期限或自动重试周期。
    bool enableOriginalCurrentAlarms(const OriginalRuntime::SystemProfile &profile,
                                     int queryWaitMilliseconds = 10000);
    bool originalAlarmShutdownComplete() const;
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
    // 只登记一次启动登录；页面忙时等待，显示或取消后不再自动重开。
    void requestStartupLogin();
    bool requestLogin();
    bool requestLogout();
    bool requestShutdown();
    bool enableDeviceControl(bool connectAtStartup = true);
    DeviceControl *deviceControl() const { return deviceControl_; }
    // 在样本/试剂页面挂入后连接实际后台；入口与延迟初始化均可重复调用。
    void attachDeviceTaskStore();
    int currentMainPage() const;
    int currentUserPage() const;
    bool selectMainPage(int index);
    bool selectUserPage(int index);
    void setDataDirectory(const QString &directory);
    void applyThemeConfiguration(const OriginalRuntime::ThemeOptions &theme);
    bool applyOriginalRuntime(const OriginalRuntime::RuntimeSettings &settings,
                              const OriginalRuntime::SystemProfile &system, QString *message);
    // 只呈现显式离线回放；不改变账户动作条件或启用任何设备按钮。
    void setStatusReplayState(const Sn100Protocol::ReplayState &state);
    // 自建本机peer的只读观测；与文件回放使用不同来源提示，不解禁设备动作。
    void setLoopbackStatusState(const Sn100Protocol::LoopbackStatusState &state);

signals:
    void navigationChanged(int mainIndex, int userIndex);
    void navigationRejected(QString reason);
    void sessionChanged(AccountSession session);
    // 运行态变化只更新既有守卫，不触发身份切换的任务清理和配置重同步。
    void deviceActionStateChanged(AccountSession session);
    void loggedOut();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    std::shared_ptr<TaskReagentRouting::Bridge> taskReagentBridge_;
    void attachTaskReagents();
    bool routeTaskReagentPacket(quint64 packet, quint64 connection, const QByteArray &payload);
    void finishTaskReagentPacket(quint64 packet, quint64 connection, const QByteArray &command, bool success, const QString &message);
    void advanceTaskReagents();
    void observeTaskReagentCommit(const OriginalTaskReagents::Commit &receipt);
    void receiveTaskReagentAdoption(const OriginalTaskReagents::RuntimeAdoption &adoption);
    void taskReagentReadbackFinished(const OriginalTaskReagents::Commit &receipt);
    void completeTaskReagentAction(quint64 actionId);
    bool queueUserTaskPreparation(quint64 request, quint64 connection, bool linuxNew, QString *message);
    bool userTaskOriginCurrent(const OriginalTaskReagents::RuntimeBasis &basis, QString *message) const;
    void finishUserTaskOperation(const OriginalTaskReagents::Commit &receipt, bool success, const QString &message);
    void cancelUserTaskPreparation(quint64 request, const QString &message);
    void finishDeviceTaskPreparation(quint64 request, bool success, const QString &message);
    void cancelTaskReagentContexts(const QString &reason);
    void taskReagentContextChanged();
    void taskReagentSourceReviewed(bool success);
    bool requestTaskReagentShutdown();
    QString taskReagentNBlocker() const;
    bool taskReagentObservationOnly(quint64 connection, bool validQj);
    quint64 taskReagentFenceToken() const;
    int taskReagentRetainedPackets() const;
    qint64 taskReagentRetainedBytes() const;
    void beginTaskReagentUrSource(const QByteArray &payload, quint64 connection);
    void beginTaskReagentManualSource(const QString &hole, quint64 connection);
    void finishTaskReagentSourceEnqueue();
    void checkTaskReagentSource();
    void reportTaskReagent(const QString &message);
    void ensureTaskReagentFence();
    void deferTaskReagentCatalogClose();
    void enterTaskReagentCloseAttempt();
    void syncTaskReagentDisplayUnknown();
    QString taskReagentTransactionBlocker() const;
    QPointer<OriginalLevels::ObservationWorker> reagentLevelWorker_;
    std::shared_ptr<OriginalLevels::Context> reagentLevelContext_, reagentLevelSource_, reagentLevelActive_;
    QString reagentLevelSourceRoot_;
    QString reagentLevelRuntimeHole_;
    quint64 reagentLevelSourceConnection_ = 0;
    quint64 reagentLevelRequestedUrConnection_ = 0;
    QMap<quint64,std::shared_ptr<OriginalLevels::Context>> reagentLevelUrContexts_;
    std::shared_ptr<OriginalLevels::Context> reagentLevelPriorityUrContext_;
    quint64 reagentLevelSourceRequest_ = 0, reagentLevelPermissionRevision_ = 0, reagentLevelPacket_ = 0;
    bool reagentLevelClosing_ = false;
    void attachReagentLevels();
    void updateReagentLevelContext(bool validQj=false);
    void reviewReagentLevelSource(const QString &root, quint64 connection);
    std::shared_ptr<OriginalLevels::Context> reagentLevelUrContext(quint64 connection) const;
    void confirmReagentLevelUr(const QByteArray &payload, quint64 connection,
                              const std::shared_ptr<OriginalLevels::Context> &arrivalContext);
    void invalidateReagentLevels();
    bool reagentLevelBusy() const;
    QString reagentLevelSyncBlocker(quint64 connection) const;
    bool requestReagentLevelShutdown();
    void reportReagentLevel(const QString &message);
    Ui::frmMain *ui;
    frmUserSet *userSet_;
    QButtonGroup *menuButtons_;
    QPointer<AccountStore> accounts_;
    AccountSession session_;
    bool logoutPending_ = false;
    bool passwordLoginEnabled_ = false;
    bool loginAfterLogoutPending_ = false;
    bool loginAfterLogoutQueued_ = false;
    // 启动自动登录与注销重登分别保存意图，不能借注销标记补启动。
    bool startupLoginPending_ = false;
    bool startupLoginQueued_ = false;
    bool startupLoginConsumed_ = false;
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
    QPointer<AlarmHistory::OriginalAlarmReceiver> originalAlarmReceiver_;
    QPointer<AlarmHistory::OriginalCurrentAlarmState> currentAlarmState_;
    int currentAlarmWaitMilliseconds_ = 10000;
    void queryCurrentAlarms();
    bool originalAlarmShutdownPending_ = false;
    bool requestOriginalAlarmShutdown();
    bool readOnlyStatusEnabled_ = false;
    QString readOnlyStatusText_, readOnlyStatusDetail_, readOnlyStatusColor_;
    QPointer<MaskDlgShutDown> shutdownDialog_;
    bool shutdownClosing_ = false;
    DeviceControl *deviceControl_ = nullptr;
    bool deviceActionReadyObserved_ = false;
    bool deviceActionReady_ = false;
    void updateDeviceActionReadiness();
    void applyDeviceActionReadiness(bool actionReady);
    bool connectDeviceAtStartup_ = false;
    void updateDeviceStatusDisplay();
    void updateDeviceControlActions();
    QString detectionStartBlocker() const;
    void updateOriginalDeviceSync();
    void invalidateDeviceTaskContext();
    void rememberReagentRecoveryContext();
    QString deviceRecoveryBlocker() const;
    void acceptOriginalProjectSave(const OriginalProjectSaveNotice &notice, quint64 intent, bool enqueue = false);
    void applyOriginalProjectUpdateState(const OriginalProjectUpdateState &state);
    void advanceOriginalProjectUpdates();
    void updateOriginalProjectReadiness();
    QString originalProjectUpdateBlocker() const;
    void reportDeviceTaskError(const QString &message);
    QString deviceUserControlBlocker(quint64 connection, bool requireFresh = true) const;
    QString reagentProbeTaskControlBlocker() const;
    void requestDeviceStart(quint64 connection, bool physicalKey, quint64 manualRequest = 0);
    void requestDevicePauseOrSilence(quint64 connection, bool pause, quint64 manualRequest = 0);
    void attachManualCommunication();
    void receiveManualCommunication(quint64 request, quint64 pageSession, quint64 connection,
                                    AccountSession identity, QByteArray payload);
    QString manualCommunicationBlocker(quint64 request) const;
    void finishManualCommunication(quint64 request, const QString &stage, const QString &message, bool terminal = true);
    void cancelManualCommunication(const QString &reason);
    quint64 manualCommunicationSession_ = 0, manualCommunicationIdentitySession_ = 0;
    quint64 manualCommunicationRequest_ = 0, manualCommunicationPageSession_ = 0;
    quint64 manualCommunicationConnection_ = 0, manualCommunicationTaskRequest_ = 0;
    AccountSession manualCommunicationIdentity_;
    QByteArray manualCommunicationPayload_;
    bool manualCommunicationSendAttempted_ = false;
    bool manualCommunicationAttached_ = false;
    void receiveDeviceStartKey(quint64 connection);
    // 开始、暂停与静音共用确认占用，避免嵌套确认继续接受另一条控制操作。
    bool deviceStartConfirmationPending_ = false;
    // 只保护退出/账户切换尝试的前置页面检查；不表示任务或液面读取已完成。
    bool deviceUserInteractionPending_ = false;
    QPointer<SampleStore> deviceTaskStore_;
    bool deviceTaskPreparing_ = false;
    quint64 deviceTaskRequest_ = 0, deviceTaskConnection_ = 0;
    quint64 deviceConfigurationConnection_ = 0;
    bool deviceConfigurationStarted_ = false, deviceConfigurationReady_ = false;
    bool deviceConfigurationBaseReady_ = false, projectUpdateReady_ = true, projectUpdatePumpQueued_ = false;
    quint64 projectUpdatePumpConnection_ = 0;
    QMap<QString, OriginalProjectUpdateState> projectUpdateNotices_;
    QString deviceConfigurationRoot_;
    // 连接重建与业务恢复分别核对；这组字段不改旧任务和试剂记录。
    quint64 deviceRecoveryRequest_ = 0, deviceRecoveryConnection_ = 0;
    QString deviceRecoveryRoot_, deviceRecoveryMessage_;
    bool deviceRecoveryPending_ = false, deviceRecoveryChecked_ = false, deviceRecoveryUnfinished_ = false;
    bool deviceReagentRecoveryRequired_ = false;
    QMap<quint64, QByteArray> devicePackets_;
    quint64 devicePacket_ = 0;
    qint64 devicePacketBytes_ = 0;
    QString deviceReagentUpdateHole_;
    quint64 deviceReagentUpdateRequest_ = 0;
    bool deviceReagentUpdateUnknown_ = false;
    enum class ReagentPriorityStage { None, Preparing, Queuing, WaitingReply, SavingReply, WaitingPage };
    ReagentPriorityStage reagentPriorityStage_ = ReagentPriorityStage::None;
    bool reagentPriorityAttached_ = false, reagentPriorityMayHaveQueued_ = false;
    quint64 reagentPriorityRequest_ = 0, reagentPriorityConnection_ = 0;
    quint64 reagentPriorityUnknownConnection_ = 0, reagentPriorityHistoryConnection_ = 0;
    AccountSession reagentPriorityIdentity_;
    QByteArray reagentPrioritySent_, reagentPriorityReply_;
    QString reagentPrioritySavedFingerprint_;
    QSet<QByteArray> reagentPriorityReplyDigests_;
    QElapsedTimer reagentPriorityClock_;
    struct ReagentProbeState
    {
        OriginalReagentProbe::Request source;
        OriginalReagentProbe::Prepared prepared;
        enum class Stage { Preparing, Queuing, Waiting } stage = Stage::Preparing;
        QElapsedTimer clock;
        bool prepareReturned=false, mayHaveQueued=false, cancelled=false, retiring=false;
        bool reported=false, actionSuccess=false, nSeen=false;
        bool nProcessed=false, nCommitted=false, nLoaded=false, nDisplayDone=false, nShown=false;
        bool nReaderReturned=true, nFinished=false;
        quint64 nPacket=0, nPermission=0;
        QByteArray completion;
        QString message;
    };
    std::shared_ptr<ReagentProbeState> reagentProbe_;
    bool reagentProbeAttached_=false, reagentProbeUnknown_=false;
    QString reagentProbeUnknownReason_;
    QTimer *reagentProbeTimer_=nullptr;
    void attachReagentProbe();
    QString reagentProbeBlocker(const QString &hole, bool ownPageBusy=false, bool executing=false) const;
    void beginReagentProbe(quint64 request,quint64 connection,const QString &hole);
    void preparedReagentProbe(const OriginalReagentProbe::Prepared &prepared);
    void cancelReagentProbe(const QString &message);
    bool routeReagentProbeReply(const QByteArray &payload,quint64 connection);
    void tickReagentProbe();
    bool reagentProbeExecutionExpired() const;
    void finishReagentProbeIfReady();
    bool reagentProbeAcceptsN(const OriginalLevels::ReceivedPacket &packet,quint64 connection) const;
    void reagentProbeNStarted(quint64 packet);
    void reagentProbeNProcessed(quint64 packet,quint64 connection,bool committed,bool loaded);
    void reagentProbeNPresented(quint64 packet,quint64 connection,bool shown);
    void reagentProbeNReaderReturned(quint64 packet,quint64 connection);
    void attachReagentPriority();
    QString reagentPositionBlocker() const;
    QString reagentPriorityBlocker(bool ownPageBusy = false) const;
    void updateReagentPriorityAccess();
    void beginReagentPriority(quint64 request, quint64 connection, const QString &operation, const QString &hole);
    void finishReagentPriority(bool success, const QString &message, bool resultUnknown);
    bool routeReagentPriorityReply(const QByteArray &payload, quint64 connection);
    void finishReagentPriorityIfDisplayed();
    void beginOriginalReagentWait(const QString &hole, const QString &message);

    void restoreMainSelection();
    void configureOfflineStatus();
    void updateAccountStatus();
    void applyAccountSession(const AccountSession &session);
    bool businessPagesIdle() const;
    bool alarmHistoryIdle() const;
    bool requestAlarmHistoryLeave(const QString &action);
    bool rejectLoginNavigation();
    void resumeLoginAfterLogout();
    void resumeStartupLogin();
    void cancelStartupLogin();
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
