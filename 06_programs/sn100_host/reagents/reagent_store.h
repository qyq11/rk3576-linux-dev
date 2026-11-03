#ifndef SN100_REAGENT_STORE_H
#define SN100_REAGENT_STORE_H
#include "data/account_store.h"
#include "reagent_types.h"
#include "samples/original_task_reagent_receipt.h"
#include <QByteArray>
#include <QMap>
#include <QObject>
#include <QSqlDatabase>
#include <QVariant>
#include <memory>
#include <chrono>
#include "projects/original_project_save_admission.h"
#include <QElapsedTimer>
class QTimer;
class OriginalReagentEditor;

// Move this object to the storage thread before initialize(). No SQL work is
// performed in the constructor or on the widget thread.
class ReagentStore : public QObject
{
    Q_OBJECT
public:
    explicit ReagentStore(QObject* parent = nullptr);
    ~ReagentStore() override;
    static void registerTypes();
    static QString validateBatchMetadata(QString isiValue, QString normalValue);
    void configureLayout(int count) { reagentPositions_=count; }
    // 这两个入口只原子替换不可变许可，可由界面线程直接调用；不访问QObject/SQL。
    void setOriginalDeviceOperationContext(quint64 connection, QStringList operations);
    bool authorizeOriginalDeviceOperation(quint64 request, quint64 connection, const QString &operation);
public slots:
    void initialize(QString absoluteCatalogDir);
    void initializeOriginal(QString root);
    void setInstrumentBusy(bool busy);
    void setSession(AccountSession session);
    void setOriginalProjectSaveAdmissionEnabled(bool enabled);
    void finishOriginalProjectSaveAdmission(OriginalProjectSaveAdmission admission);
    void load();
    void loadOriginalLevelObservation(QString root); // N专用重读：持原两锁并拒pending，不触发复制恢复。
    void loadOriginalTaskReagentCommit(OriginalTaskReagents::Commit receipt);
    void saveReagent(ReagentRecord reagent);
    void removeReagent(qint64 id);
    void saveBottle(ReagentBottle bottle);
    void removeBottle(qint64 id);
    void saveBatch(ReagentBatch batch);
    void saveOriginalBatchGroupEnabled(ReagentProject project, bool enabled);
    void removeBatch(qint64 id);
    // Reserve before an external database commits its record; release only
    // after that record is removed. These calls are NOT a cross-DB transaction.
    void holdBatchReference(qint64 requestId, QString ownerModule, QString referenceKey, qint64 batchId);
    void releaseBatchReference(qint64 requestId, QString ownerModule, QString referenceKey);
    void checkBatchReferences(qint64 requestId, qint64 batchId);
    void saveBarcode(ReagentBarcode barcode);
    void removeBarcode(qint64 id);
    void saveHole(ReagentHole hole);
    void clearHole(QString name);
    // 主窗口先核设备状态及回读能力；此槽只准备原命令和原本地资料变化。
    void prepareOriginalDeviceOperation(quint64 request, quint64 connection, QString operation, QString holeName, bool linuxNew);
    // 仅接本次优先使用的一次Ur；数据库提交与页面重读结果分别返回。
    void saveOriginalPriorityReply(quint64 request, quint64 connection, QByteArray payload);
    void close();
signals:
    void ready();
    void originalProfileReady(QString root, ReagentProjects projects);
    void originalProjectSaveAdmissionRequested(quint64 ticket, OriginalProjectSaveNotice proposal, qint64 sourceDeadline);
    void originalProjectSaveAdmissionCancelled(quint64 ticket, QString source);
    void originalProjectSaveCommitted(OriginalProjectSaveNotice notice);
    void originalProjectGroupEnableCommitted(ReagentProject project); // 本地已提交，指纹留空，重读前状态未知。
    void originalHoleSaved(QString holeName); // 提交成功后，新协议由主入口接Us，旧协议保持原本地保存。
    void originalDeviceOperationPrepared(quint64 request, quint64 connection, QString operation,
                                         QList<QByteArray> packets, bool localSaved);
    void originalDevicePreparationFailed(quint64 request, quint64 connection, QString operation, QString message);
    void originalPriorityReplyProcessed(quint64 request, quint64 connection, bool committed, bool loaded,
                                        QString fingerprint, QString message);
    void loaded(ReagentSnapshot snapshot);
    // 只表示本次任务提交后的资料读取认证；不替代GUI动作/revision核对或设备完成。
    void originalTaskReagentReadback(OriginalTaskReagents::Commit receipt, bool verified,
                                     ReagentSnapshot snapshot, ReagentProjects projects, QString message);
    void succeeded(QString operation);
    void error(QString operation, QString message);
    void closed();
    void batchReferenceSucceeded(qint64 requestId, QString operation);
    void batchReferenceError(qint64 requestId, QString operation, QString message);
    void batchReferencesReady(qint64 requestId, qint64 batchId, bool exists, ReagentBatchReferences references);

private:
    struct OriginalDeviceOperationPermit
    {
        quint64 connection = 0, request = 0;
        bool preparationClaimed = false;
        std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max();
        QStringList operations;
        QString operation;
    };
    std::shared_ptr<const OriginalDeviceOperationPermit> deviceOperationPermit_;
    struct OriginalPriorityReplyContext
    {
        quint64 request = 0, connection = 0;
        QByteArray sentPayload;
        QMap<QString,QString> versions;
        std::shared_ptr<const OriginalDeviceOperationPermit> permit;
    };
    // 只在数据库线程读取和消费；界面线程撤销的是上面的原子许可，不访问此缓存。
    OriginalPriorityReplyContext priorityReply_;
    int reagentPositions_ = 20;
    QSqlDatabase database_;
    QString connectionName_;
    AccountSession session_;
    bool ready_ = false;
    std::unique_ptr<OriginalReagentEditor> originalEditor_;
    bool instrumentBusy_ = false;
    bool projectSaveAdmissionEnabled_ = false;
    struct PendingOriginalGroupSave
    {
        quint64 ticket=0;
        ReagentProject project;
        bool enabled=false;
        AccountSession actor;
        qint64 deadline=0;
    } pendingOriginalGroupSave_;
    QTimer *projectSaveAdmissionTimer_=nullptr;
    void cancelPendingOriginalGroupSave(QString message);
    void commitOriginalGroupSave(ReagentProject project, bool enabled,
        const std::shared_ptr<OriginalProjectSavePermit> &permit = {});
    bool originalMayEdit(const QString &operation);
    void finishOriginal(const QString &operation, bool success, const QString &message);
    bool loadOriginal();
    bool onWorker(const QString& operation);
    bool begin(const QString& operation);
    bool run(const QString& sql, const QVariantList& values, QString* message);
    bool finish(const QString& operation, bool success, const QString& message);
    bool remove(
        const QString& table, qint64 id, const QString& operation, const QString& extraWhere = QString());
    bool referenceReady(qint64 requestId, const QString& operation, bool write);
};
#endif
