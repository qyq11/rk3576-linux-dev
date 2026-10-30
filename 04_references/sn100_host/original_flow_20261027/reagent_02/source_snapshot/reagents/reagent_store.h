#ifndef SN100_REAGENT_STORE_H
#define SN100_REAGENT_STORE_H
#include "data/account_store.h"
#include "reagent_types.h"
#include <QObject>
#include <QSqlDatabase>
#include <QVariant>
#include <memory>
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
public slots:
    void initialize(QString absoluteCatalogDir);
    void initializeOriginal(QString root);
    void setInstrumentBusy(bool busy);
    void setSession(AccountSession session);
    void load();
    void saveReagent(ReagentRecord reagent);
    void removeReagent(qint64 id);
    void saveBottle(ReagentBottle bottle);
    void removeBottle(qint64 id);
    void saveBatch(ReagentBatch batch);
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
    void close();
signals:
    void ready();
    void originalProfileReady(QString root, ReagentProjects projects);
    void loaded(ReagentSnapshot snapshot);
    void succeeded(QString operation);
    void error(QString operation, QString message);
    void closed();
    void batchReferenceSucceeded(qint64 requestId, QString operation);
    void batchReferenceError(qint64 requestId, QString operation, QString message);
    void batchReferencesReady(qint64 requestId, qint64 batchId, bool exists, ReagentBatchReferences references);

private:
    QSqlDatabase database_;
    QString connectionName_;
    AccountSession session_;
    bool ready_ = false;
    std::unique_ptr<OriginalReagentEditor> originalEditor_;
    bool instrumentBusy_ = false;
    bool originalMayEdit(const QString &operation);
    void finishOriginal(const QString &operation, bool success, const QString &message);
    void loadOriginal();
    bool onWorker(const QString& operation);
    bool begin(const QString& operation);
    bool run(const QString& sql, const QVariantList& values, QString* message);
    bool finish(const QString& operation, bool success, const QString& message);
    bool remove(
        const QString& table, qint64 id, const QString& operation, const QString& extraWhere = QString());
    bool referenceReady(qint64 requestId, const QString& operation, bool write);
};
#endif
