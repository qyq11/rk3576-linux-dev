#ifndef SN100_REAGENTS_PAGE_H
#define SN100_REAGENTS_PAGE_H
#include "reagent_store.h"
#include "original_reagent_probe.h"
#include "original_task_reagent_display.h"
#include <QPointer>
#include <QHash>
#include <QWidget>
#include <chrono>
#include <memory>
class QDialog;
class QTimer;
class OriginalReagentUsageDialog;
namespace OriginalLevels { class RuntimeProvider; struct CurrentHoleRuntime; struct ObservationPresentation; }
namespace Ui
{
class frmReagentMaterial;
}
class ReagentsPage : public QWidget
{
    Q_OBJECT
public:
    explicit ReagentsPage(ReagentStore* store, QWidget* parent = nullptr);
    ~ReagentsPage() override;
    bool isReady() const
    {
        return ready_;
    }
    bool isBusy() const
    {
        return busy_;
    }
    bool hasUnsavedChanges() const
    {
        return dirty_;
    }
    bool prepareToLeave();
    ReagentSnapshot snapshot() const
    {
        return snapshot_;
    }
    ReagentStore *taskStore() const { return store_; }
    bool beginOriginalProbe(OriginalReagentProbe::Request source);
    bool originalProbeOwnBusy(const OriginalReagentProbe::Request &source) const;
    bool grantOriginalProbeN(const OriginalReagentProbe::Prepared &source, quint64 packet);
    bool originalProbeNMatches(quint64 packet, quint64 connection, const QString &root, const QString &hole) const;
    void stopOriginalProbeN(const OriginalReagentProbe::Request &source);
    void retireOriginalProbeN(const OriginalReagentProbe::Request &source, quint64 packet);
    bool finishOriginalProbe(const OriginalReagentProbe::Request &source, bool success, const QString &message);
    void configureLayout(int count);
    OriginalLevels::CurrentHoleRuntime originalLevelRuntime(const QString &hole) const;
    QString originalLevelRuntimeReason(const QString &hole) const;
    void setOriginalLevelContext(quint64 connection, quint64 permissionRevision);
    void confirmOriginalLevelUr(const QByteArray &payload, bool linuxNew, quint64 connection);
    void confirmOriginalLevelManual(const QString &hole, quint64 connection);
    bool enqueueOriginalLevelPresentation(const OriginalLevels::ObservationPresentation &presentation, QString *message);
    bool hasPendingOriginalLevelPresentation() const { return bool(pendingOriginalLevelPresentation_); }
    bool hasUnconfirmedOriginalLevelRuntime() const
    { return hasPendingOriginalLevelPresentation() || !pendingOriginalLevelUr_.isEmpty() || !pendingOriginalLevelManual_.isEmpty(); }
    void notifyOriginalLevel(const QString &message);
    void originalLevelReloadFailed(const QString &message);
    void rejectOriginalLevelObservation(const QString &hole, bool committed, const QString &message);
    // GUI线程直接登记任务身份；不签发N观察许可或替代本页会话/资料核对。
    void setOriginalTaskReagentIdentity(OriginalTaskReagents::TaskIdentity identity);
    // 只遮住当前身份下的待核显示；不修改运行值、SQL、账户或N写入许可。
    void setOriginalTaskReagentDisplayUnknown(quint64 identityRevision, QString root, QStringList holes,
                                               bool unscoped, QString message);
    QStringList originalTaskReagentActiveHoleNames() const { return originalTaskReagentActiveHoles(); }
    OriginalTaskReagents::RuntimeBasis freezeOriginalTaskReagentBasis(OriginalTaskReagents::RuntimeBasis seed, QString *message);
    bool enqueueOriginalTaskReagentCommit(OriginalTaskReagents::Commit receipt, QString *message);
    void cancelOriginalTaskReagentBasis(const QString &token, const QString &reason);
public slots:
    void setProjects(ReagentProjects projects);
    void setSession(AccountSession session);
    void openReagentEditor();
    void openHoleEditor();
    void openBatchEditor();
    void openBottleEditor();
    void reloadOriginal();
    void refreshOriginalPageMetadata();
    void setInstrumentBusy(bool busy);
    // 由主窗口在核对真实连接和已接好的回读流程后开放动作；断线时传0和空清单。
    void setDeviceOperations(quint64 connection, QStringList operations);
    void deviceOperationProgress(quint64 request, quint64 connection, QString message);
    void renewDeviceOperationWait(quint64 request, quint64 connection);
    void finishDeviceOperation(quint64 request, quint64 connection, bool success, QString message);
    void updateOriginalUsage(quint64 connection, QHash<QString,QString> projectHas, QHash<QString,QString> projectUsed);
    void updateOriginalDeviceKey(quint64 connection, QString key);
    void finishOriginalUsageImport(quint64 connection, bool success);
signals:
    void originalTaskReagentAdopted(OriginalTaskReagents::RuntimeAdoption result);
    void originalLevelRuntimeChanged(); // 原运行来源改变时立即撤销在途N的旧依据。
    void originalLevelPresentationFinished(quint64 packetId, quint64 connection, quint64 permissionRevision,
                                         bool shown, QString fingerprint, QString message);
    void snapshotChanged(ReagentSnapshot snapshot);
    void readyChanged(bool ready);
    void busyChanged(bool busy);
    void operationMessage(QString operation, QString message);
    void operationFinished(QString operation, bool success);
    void requestLoad();
    void requestSession(AccountSession session);
    void requestSaveReagent(ReagentRecord reagent);
    void requestRemoveReagent(qint64 id);
    void requestSaveBottle(ReagentBottle bottle);
    void requestRemoveBottle(qint64 id);
    void requestSaveBatch(ReagentBatch batch);
    void requestSaveOriginalBatchGroupEnabled(ReagentProject project, bool enabled);
    void originalProjectGroupEnabledSaved(ReagentProject project); // 本地提交后的实际重读身份及新指纹，不表示仪器已同步。
    void requestRemoveBatch(qint64 id);
    void requestSaveBarcode(ReagentBarcode barcode);
    void requestRemoveBarcode(qint64 id);
    void requestSaveHole(ReagentHole hole);
    void requestClearHole(QString name);
    // 这里只提交操作意图；主窗口检查设备状态、发命令并核回读后再通知结果。
    void deviceOperationRequested(quint64 request, quint64 connection, QString operation, QString holeName);
    void usageCommandRequested(quint64 connection, QByteArray payload);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void initializeOriginalTaskReagentDisplay();
    QStringList originalTaskReagentActiveHoles() const;
    bool originalTaskReagentContextMatches(QString *message) const;
    bool originalTaskIdentityMatchesBasis(const OriginalTaskReagents::RuntimeBasis &basis) const;
    bool originalTaskViewDataMatches(const QVariantMap &view, const OriginalLevels::CurrentHoleRuntime &runtime) const;
    bool originalTaskViewMatches(const QVariantMap &view, const OriginalLevels::CurrentHoleRuntime &runtime) const;
    QVariantMap originalTaskIdentitySourceView(const QVariantMap &view, const OriginalLevels::CurrentHoleRuntime &runtime) const;
    bool originalTaskReagentDisplayUnknown(const QString &hole) const;
    QString originalTaskReagentDisplayUnknownReason() const;
    void finishOriginalTaskReagentDisplay(const OriginalTaskReagents::Commit &receipt,
                                          OriginalTaskReagents::RuntimeAdoption result);
    OriginalTaskReagents::RuntimeAdoption rejectOriginalTaskReagentCommit(
        const OriginalTaskReagents::Commit &receipt, const QString &reason);
    std::shared_ptr<const OriginalTaskReagents::RuntimeBasis> originalTaskReagentBasis_;
    std::shared_ptr<const OriginalTaskReagents::Commit> originalTaskReagentCommit_;
    std::shared_ptr<const OriginalTaskReagents::RuntimeBasis> cancelledOriginalTaskReagentBasis_;
    AccountSession originalTaskReagentSession_;
    AccountSession originalTaskReagentLastSession_;
    OriginalTaskReagents::TaskIdentity originalTaskReagentIdentity_, originalTaskReagentLastIdentity_;
    AccountSession originalTaskReagentIdentitySession_;
    quint64 originalTaskReagentUnknownIdentityRevision_ = 0;
    QString originalTaskReagentUnknownRoot_, originalTaskReagentUnknownMessage_;
    QStringList originalTaskReagentUnknownHoles_;
    bool originalTaskReagentUnknownUnscoped_ = false;
    std::shared_ptr<const char> originalTaskReagentProviderToken_;
    std::weak_ptr<const char> originalTaskReagentBasisProviderToken_, cancelledOriginalTaskReagentProviderToken_,
        originalTaskReagentLastProviderToken_;
    ReagentSnapshot originalTaskReagentSnapshot_;
    ReagentProjects originalTaskReagentProjects_;
    QMap<QString,QVariantMap> originalTaskReagentViews_;
    QString originalTaskReagentCancelled_;
    quint64 originalTaskReagentActionFloor_ = 0;
    QByteArray originalTaskReagentReceiptSignature_;
    std::shared_ptr<const OriginalTaskReagents::RuntimeAdoption> originalTaskReagentLastAdoption_;
    QTimer *originalTaskReagentTimer_ = nullptr;
    bool originalTaskReagentOwnsBusy_ = false;
    std::chrono::steady_clock::time_point originalTaskReagentDeadline_;
    std::unique_ptr<OriginalLevels::RuntimeProvider> originalLevelRuntime_;
    std::shared_ptr<const OriginalLevels::ObservationPresentation> pendingOriginalLevelPresentation_;
    QMap<QString,QVariantMap> originalLevelViews_;
    QList<QPair<QByteArray,bool>> pendingOriginalLevelUr_;
    QStringList pendingOriginalLevelManual_;
    QStringList failedOriginalLevelHoles_;
    bool originalLevelProjectCountsVerified_ = true;
    quint64 originalLevelConnection_ = 0, originalLevelPermissionRevision_ = 0;
    std::chrono::steady_clock::time_point originalLevelFreshUntil_;
    void consumeOriginalLevelSnapshot();
    void renderOriginalLevelViews();
    Ui::frmReagentMaterial* ui;
    ReagentStore *store_ = nullptr;
    OriginalReagentProbe::Request probeOwner_;
    OriginalReagentProbe::NGrant probeN_;
    bool probeAcceptingN_ = false;
    bool originalProfile_ = false;
    bool instrumentBusy_ = false, pendingOriginalReload_ = false;
    QString pendingSavedOperation_;
    ReagentSnapshot snapshot_;
    ReagentProjects projects_;
    ReagentProject pendingOriginalGroupCommit_; // 已提交但尚未重读；不能把旧开关当作当前值。
    AccountSession session_;
    QPointer<QDialog> dialog_;
    QPointer<OriginalReagentUsageDialog> usageDialog_;
    bool ready_ = false, busy_ = false, dirty_ = false;
    QString selectedHole_ = QStringLiteral("R_1");
    quint64 deviceConnection_ = 0, nextDeviceRequest_ = 0, pendingDeviceRequest_ = 0;
    QStringList deviceOperations_;
    QString pendingDeviceOperation_;
    QTimer *deviceOperationTimer_ = nullptr;
    void selectHole(QString name);
    QString originalPageBatch(const ReagentHole &hole) const;
    void setBusy(bool busy);
    void showOperationMessage(const QString &operation, const QString &message);
    void requestOriginalDeviceOperation(const QString &operation, const QString &question);
    void refreshTestCounts();
    void openUsageDialog();
    bool canEdit() const;
    void applyPermissions();
    bool allowEditor();
    void initializeDialog(QDialog* dialog);
    void markDirty();
    void openBarcodeEditor(qint64 reagentId, qint64 barcodeId = 0);
};
#endif
