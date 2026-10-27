#ifndef SN100_SAMPLE_STORE_H
#define SN100_SAMPLE_STORE_H
#include "data/account_store.h"
#include "sample_types.h"
#include <QObject>
#include <QSqlDatabase>

// SQL is opened, used and closed only in this object's worker thread.
class SampleStore : public QObject
{
    Q_OBJECT
  public:
    explicit SampleStore(QObject *parent = nullptr);
    ~SampleStore() override;
    static void registerTypes();
    static bool canEdit(const AccountSession &session);
  public slots:
    void initialize(QString catalogDir);
    void setSession(AccountSession session);
    void load(qint64 requestId);
    void saveRack(qint64 requestId, SampleRack rack);
    void removeRack(qint64 requestId, qint64 rackId);
    void clearOrdinaryRacks(qint64 requestId);
    void saveCombination(qint64 requestId, SampleCombination combination);
    void removeCombination(qint64 requestId, qint64 combinationId);
    void close();
  signals:
    void ready(QString databasePath);
    void snapshotReady(qint64 requestId, SampleSnapshot snapshot);
    void succeeded(qint64 requestId, QString operation, qint64 id);
    void error(qint64 requestId, QString operation, QString message);
    void closed();

  private:
    bool check(qint64 requestId, const QString &operation, bool write = false);
    bool read(SampleSnapshot *snapshot, QString *message, qint64 onlyRack = 0);
    QString validateRack(const SampleRack &rack);
    bool finish(qint64 requestId, const QString &operation, qint64 id, bool ok, QString message);
    void release();
    QSqlDatabase database_;
    QString connection_;
    AccountSession session_;
    bool ready_ = false;
};
#endif
