#ifndef SN100_CALIBRATION_STORE_H
#define SN100_CALIBRATION_STORE_H

#include "calibration_types.h"
#include "data/account_store.h"
#include "projects/projects_store.h"
#include "reagents/reagent_types.h"
#include <QObject>
#include <QSqlDatabase>
#include <memory>

class QLockFile;

// Construct without parent, move to its SQL worker, then initialize there.
// 原始点仅写入本地库；同worker另有项目库连接，仅写已定义的引用预约表。
// 先预约、后提交本地记录；两个文件不具有跨库外键或一次原子提交。
class CalibrationStore : public QObject
{
    Q_OBJECT
public:
    explicit CalibrationStore(QObject *parent = nullptr);
    ~CalibrationStore() override;
    static void registerTypes();
    static bool canEdit(const AccountSession &session);
    static QString validate(const CalibrationRecord &record);

public slots:
    void initialize(QString absoluteDataDir, QString catalogDatabasePath);
    void setSession(AccountSession session);
    void loadCatalog(qint64 requestId);
    void load(qint64 requestId);
    void save(qint64 requestId, CalibrationRecord record);
    void copy(qint64 requestId, qint64 sourceId, qint64 sourceRevision);
    void remove(qint64 requestId, qint64 id, qint64 revision);
    void close();

signals:
    void ready(QString databasePath);
    void catalogReady(qint64 requestId, ProjectRecords projects, ProjectUnits units, ReagentSnapshot reagents);
    void snapshotReady(qint64 requestId, CalibrationRecords records);
    void succeeded(qint64 requestId, QString operation, qint64 id);
    void error(qint64 requestId, QString operation, QString message);
    void closed();

private:
    bool check(qint64 requestId, const QString &operation, bool write = false);
    bool read(CalibrationRecords *records, QString *message, bool includeHidden = false);
    bool checkCatalog(CalibrationRecord *record, bool existingIdentity, QString *message,
                      QSqlDatabase catalog = QSqlDatabase(), bool allowDisabledHistory = false);
    bool initializeReferenceProtection(QString catalogPath, QString *message);
    bool reserveReferences(CalibrationRecords *records, bool historicalIdentity,
                           QStringList *newProjects, QStringList *newBatches, QString *message,
                           bool allowDisabledHistory = false);
    bool releaseNewReferences(const QStringList &projects, const QStringList &batches, QString *message);
    void cleanupFailedReferences(const QStringList &projects, const QStringList &batches, QString *message);
    std::unique_ptr<QLockFile> lockDatasetOperation(QString *message);
    bool checkDatasetPathBinding(QString *message);
    QString referenceKey(const CalibrationRecord &record) const;
    bool writeRecord(CalibrationRecord record, qint64 *id, QString *message);
    bool beginWrite(QString *message);
    void fail(qint64 requestId, const QString &operation, const QString &message);
    void release();
    QSqlDatabase m_database;
    QSqlDatabase m_catalog;
    QSqlDatabase m_referenceCatalog;
    QString m_connectionName;
    QString m_catalogConnectionName;
    QString m_referenceCatalogConnectionName;
    QString m_datasetId;
    QString m_datasetPath;
    AccountSession m_session;
    bool m_ready = false;
};

#endif
