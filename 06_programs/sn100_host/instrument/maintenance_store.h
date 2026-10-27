#ifndef SN100_MAINTENANCE_STORE_H
#define SN100_MAINTENANCE_STORE_H

#include "data/account_store.h"
#include <QDate>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QSqlDatabase>

struct MaintenanceRecord
{
    QString period;
    QString date;
    QString action;
};

using MaintenanceRecords = QList<MaintenanceRecord>;
Q_DECLARE_METATYPE(MaintenanceRecord)
Q_DECLARE_METATYPE(MaintenanceRecords)

// The entry point owns this worker and its thread. All slots, including close,
// are queued to that thread; no QWidget or GUI SQL connection is shared here.
class MaintenanceStore : public QObject
{
    Q_OBJECT

public:
    explicit MaintenanceStore(QObject *parent = nullptr);
    ~MaintenanceStore() override;
    static void registerTypes();

public slots:
    // Uses only <absoluteDataDir>/maintenance.db, never a reference database.
    void initialize(QString absoluteDataDir);
    void setSession(AccountSession session);
    void loadMonth(QDate month);
    // Manual completion records only. Device washing/shutdown cannot be forged.
    void record(QString action, QDate date);
    void close();

signals:
    void ready();
    void loaded(MaintenanceRecords records);
    void succeeded(QString action);
    void error(QString operation, QString message);
    void closed();

private:
    bool checkThread(const QString &operation);
    bool checkReady(const QString &operation);
    void releaseDatabase();

    QSqlDatabase m_database;
    QString m_connectionName;
    AccountSession m_session;
    bool m_ready = false;
};

#endif
