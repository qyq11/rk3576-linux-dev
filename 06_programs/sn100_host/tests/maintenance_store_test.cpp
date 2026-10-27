#include "instrument/maintenance_store.h"

#include <QtTest>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <atomic>

namespace
{
class Worker
{
public:
    Worker() : store(new MaintenanceStore), ready(store, &MaintenanceStore::ready),
        loaded(store, &MaintenanceStore::loaded), saved(store, &MaintenanceStore::succeeded),
        errors(store, &MaintenanceStore::error), closed(store, &MaintenanceStore::closed)
    {
        store->moveToThread(&thread);
        QObject::connect(&thread, &QThread::finished, store, &QObject::deleteLater);
        thread.start();
    }
    ~Worker()
    {
        QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
        thread.quit();
        thread.wait();
    }
    void initialize(const QString &directory)
    {
        QMetaObject::invokeMethod(store, "initialize", Qt::QueuedConnection, Q_ARG(QString, directory));
    }
    void session(int role = 2, bool authenticated = true)
    {
        AccountSession value;
        value.name = QStringLiteral("anonymous-maintenance-operator");
        value.role = role;
        value.authenticated = authenticated;
        value.actionReady = false; // No real-device readiness is implied.
        QMetaObject::invokeMethod(store, "setSession", Qt::QueuedConnection, Q_ARG(AccountSession, value));
    }
    void record(const QString &action, const QDate &date = QDate::currentDate())
    {
        QMetaObject::invokeMethod(store, "record", Qt::QueuedConnection,
                                  Q_ARG(QString, action), Q_ARG(QDate, date));
    }
    void month(const QDate &date)
    {
        QMetaObject::invokeMethod(store, "loadMonth", Qt::QueuedConnection, Q_ARG(QDate, date));
    }
    void close()
    {
        QMetaObject::invokeMethod(store, "close", Qt::QueuedConnection);
    }
    MaintenanceStore *store;
    QThread thread;
    QSignalSpy ready, loaded, saved, errors, closed;
};

class Database
{
public:
    explicit Database(const QString &path)
    {
        name = QUuid::createUuid().toString();
        value = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        value.setDatabaseName(path);
        value.open();
    }
    ~Database()
    {
        value.close();
        value = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
    }
    QVariant scalar(const QString &sql)
    {
        QSqlQuery query(value);
        if (!query.exec(sql) || !query.next())
            return QVariant();
        return query.value(0);
    }
    bool exec(const QString &sql)
    {
        QSqlQuery query(value);
        return query.exec(sql);
    }
    QString name;
    QSqlDatabase value;
};

QByteArray fileBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    return file.readAll();
}

class SnapshotWriter : public QThread
{
public:
    explicit SnapshotWriter(QString databasePath) : path(databasePath) {}
    ~SnapshotWriter() override { stop(); }
    void stop()
    {
        stopped.store(true);
        wait();
    }
    void run() override
    {
        Database database(path);
        if (!database.value.isOpen())
        {
            failure = database.value.lastError().text();
            return;
        }
        while (!stopped.load())
        {
            if (!database.value.transaction())
            {
                failure = database.value.lastError().text();
                break;
            }
            const QString value = committed.load() % 2 == 0 ? QStringLiteral("") : QStringLiteral("1");
            bool success = true;
            for (const QString &sql : {
                QStringLiteral("UPDATE day_maintain SET waste_cup=?,waste_water=?,waste_cup_box=?"),
                QStringLiteral("UPDATE week_maintain SET clean_pool=?,clean_dirver=?"),
                QStringLiteral("UPDATE month_maintain SET clean_needle=?")})
            {
                QSqlQuery query(database.value);
                query.prepare(sql);
                for (int index = 0; index < sql.count(QLatin1Char('?')); ++index)
                    query.addBindValue(value);
                if (!query.exec())
                {
                    failure = query.lastError().text();
                    success = false;
                    break;
                }
            }
            if (!success || !database.value.commit())
            {
                if (failure.isEmpty()) failure = database.value.lastError().text();
                database.value.rollback();
                break;
            }
            committed.fetch_add(1);
            QThread::msleep(1);
        }
    }
    QString path;
    QString failure; // Read only after stop()/wait().
    std::atomic<int> committed{0};
    std::atomic<bool> stopped{false};
};
}

class MaintenanceStoreTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_root = qEnvironmentVariable("SN100_MAINTENANCE_TEST_ROOT");
        QVERIFY2(QDir::isAbsolutePath(m_root), "Set SN100_MAINTENANCE_TEST_ROOT to an isolated absolute temp directory");
        QVERIFY(QDir().mkpath(m_root));
        MaintenanceStore::registerTypes();
    }

    void savesAllManualPeriodsAndReopens()
    {
        QTemporaryDir directory(m_root + QStringLiteral("/save-XXXXXX"));
        QVERIFY(directory.isValid());
        const QString path = directory.path() + QStringLiteral("/maintenance.db");
        const QStringList actions{QStringLiteral("waste_cup"), QStringLiteral("waste_water"),
            QStringLiteral("waste_cup_box"), QStringLiteral("clean_pool"),
            QStringLiteral("clean_dirver"), QStringLiteral("clean_needle")};
        {
            Worker worker;
            worker.initialize(directory.path());
            QTRY_COMPARE(worker.ready.count(), 1);
            worker.session();
            for (const QString &action : actions)
                worker.record(action);
            QTRY_COMPARE(worker.saved.count(), actions.size());
            QCOMPARE(worker.errors.count(), 0);
            worker.month(QDate::currentDate());
            QTRY_COMPARE(worker.loaded.count(), 1);
            const MaintenanceRecords records = qvariant_cast<MaintenanceRecords>(worker.loaded.last().at(0));
            QCOMPARE(records.size(), 6);
            QSet<QString> returnedActions, periods;
            for (const MaintenanceRecord &record : records)
            {
                returnedActions.insert(record.action);
                periods.insert(record.period);
                QCOMPARE(record.date, QDate::currentDate().toString(Qt::ISODate));
            }
            QCOMPARE(returnedActions, QSet<QString>(actions.begin(), actions.end()));
            QCOMPARE(periods, QSet<QString>({QStringLiteral("day"), QStringLiteral("week"), QStringLiteral("month")}));
            worker.record(QStringLiteral("waste_cup"));
            QTRY_COMPARE(worker.saved.count(), 7);
            worker.close();
            QTRY_COMPARE(worker.closed.count(), 1);
        }
        {
            Database database(path);
            QVERIFY(database.value.isOpen());
            QCOMPARE(database.scalar(QStringLiteral("SELECT count(*) FROM day_maintain")).toInt(), 1);
            QCOMPARE(database.scalar(QStringLiteral("SELECT count(*) FROM week_maintain")).toInt(), 1);
            QCOMPARE(database.scalar(QStringLiteral("SELECT count(*) FROM month_maintain")).toInt(), 1);
            QCOMPARE(database.scalar(QStringLiteral("SELECT long_clean IS NULL AND shut_down IS NULL FROM day_maintain")).toInt(), 1);
            QCOMPARE(database.scalar(QStringLiteral("SELECT waste_cup||waste_water||waste_cup_box FROM day_maintain")).toString(), QStringLiteral("111"));
            QCOMPARE(database.scalar(QStringLiteral("PRAGMA integrity_check")).toString(), QStringLiteral("ok"));
        }
        Worker reopened;
        reopened.initialize(directory.path());
        QTRY_COMPARE(reopened.ready.count(), 1);
        reopened.month(QDate::currentDate());
        QTRY_COMPARE(reopened.loaded.count(), 1);
        QCOMPARE(qvariant_cast<MaintenanceRecords>(reopened.loaded.last().at(0)).size(), 6);
        reopened.month(QDate::currentDate().addMonths(-1));
        QTRY_COMPARE(reopened.loaded.count(), 2);
        QVERIFY(qvariant_cast<MaintenanceRecords>(reopened.loaded.last().at(0)).isEmpty());
    }

    void rejectsUnauthorizedMachineAndInvalidDates()
    {
        QTemporaryDir directory(m_root + QStringLiteral("/reject-XXXXXX"));
        Worker worker;
        worker.initialize(directory.path());
        QTRY_COMPARE(worker.ready.count(), 1);
        worker.record(QStringLiteral("waste_cup"));
        QTRY_COMPARE(worker.errors.count(), 1);
        worker.session(6);
        worker.record(QStringLiteral("waste_cup"));
        QTRY_COMPARE(worker.errors.count(), 2);
        worker.session(1);
        const QStringList rejected{QStringLiteral("long_clean"), QStringLiteral("shut_down"),
                                  QStringLiteral("unknown"), QStringLiteral("waste_cup=1;DROP TABLE day_maintain")};
        for (const QString &action : rejected)
            worker.record(action);
        worker.record(QStringLiteral("waste_cup"), QDate());
        worker.record(QStringLiteral("waste_cup"), QDate::currentDate().addDays(-1));
        worker.record(QStringLiteral("waste_cup"), QDate::currentDate().addDays(1));
        QTRY_COMPARE(worker.errors.count(), 9);
        QCOMPARE(worker.saved.count(), 0);
        worker.month(QDate());
        QTRY_COMPARE(worker.errors.count(), 10);
        worker.month(QDate::currentDate());
        QTRY_COMPARE(worker.loaded.count(), 1);
        QVERIFY(qvariant_cast<MaintenanceRecords>(worker.loaded.last().at(0)).isEmpty());
        worker.record(QStringLiteral("waste_cup"));
        QTRY_COMPARE(worker.saved.count(), 1); // ordinary operator, actionReady=false
        worker.close();
        QTRY_COMPARE(worker.closed.count(), 1);
        worker.record(QStringLiteral("waste_cup"));
        QTRY_COMPARE(worker.errors.count(), 11);
    }

    void lockFailureKeepsEventLoopResponsiveAndRecovers()
    {
        QTemporaryDir directory(m_root + QStringLiteral("/lock-XXXXXX"));
        Worker worker;
        worker.initialize(directory.path());
        QTRY_COMPARE(worker.ready.count(), 1);
        worker.session();
        worker.record(QStringLiteral("waste_cup"));
        QTRY_COMPARE(worker.saved.count(), 1);
        Database blocker(directory.path() + QStringLiteral("/maintenance.db"));
        QVERIFY(blocker.exec(QStringLiteral("BEGIN IMMEDIATE")));
        int ticks = 0;
        QTimer heartbeat;
        heartbeat.setInterval(10);
        connect(&heartbeat, &QTimer::timeout, this, [&ticks]() { ++ticks; });
        heartbeat.start();
        QElapsedTimer elapsed;
        elapsed.start();
        worker.record(QStringLiteral("clean_pool"));
        QTRY_COMPARE(worker.errors.count(), 1);
        const qint64 lockWaitMs = elapsed.elapsed();
        QVERIFY(lockWaitMs >= 150);
        QVERIFY(lockWaitMs < 2000);
        QVERIFY(ticks >= 5);
        heartbeat.stop();
        QCOMPARE(worker.saved.count(), 1);
        QVERIFY(worker.errors.last().at(1).toString().contains(QStringLiteral("locked")));
        QVERIFY(blocker.exec(QStringLiteral("ROLLBACK")));
        worker.record(QStringLiteral("clean_pool"));
        QTRY_COMPARE(worker.saved.count(), 2);
        worker.month(QDate::currentDate());
        QTRY_COMPARE(worker.loaded.count(), 1);
        QCOMPARE(qvariant_cast<MaintenanceRecords>(worker.loaded.last().at(0)).size(), 2);
        m_measurements[QStringLiteral("lock_wait_ms")] = double(lockWaitMs);
        m_measurements[QStringLiteral("event_loop_ticks_during_lock_wait")] = ticks;
    }

    void refusesUnsafePathsAndUnmarkedExistingData()
    {
        QTemporaryDir directory(m_root + QStringLiteral("/paths-XXXXXX"));
        Worker worker;
        worker.initialize(QStringLiteral("relative-directory"));
        QTRY_COMPARE(worker.errors.count(), 1);
        const QString reference = directory.path() + QStringLiteral("/sn100_host_reference/not-created");
        worker.initialize(reference);
        QTRY_COMPARE(worker.errors.count(), 2);
        QVERIFY(!QFileInfo::exists(reference));
        QFile existing(directory.path() + QStringLiteral("/maintenance.db"));
        QVERIFY(existing.open(QIODevice::WriteOnly));
        const QByteArray original("existing-unmarked-file-must-stay-byte-identical");
        QCOMPARE(existing.write(original), qint64(original.size()));
        existing.close();
        worker.initialize(directory.path());
        QTRY_COMPARE(worker.errors.count(), 3);
        QCOMPARE(fileBytes(existing.fileName()), original);
        QVERIFY(!QFileInfo::exists(directory.path() + QStringLiteral("/offline-data.tag")));
        QCOMPARE(worker.ready.count(), 0);
    }

    void rejectsBrokenSchemaWithoutConvertingIt()
    {
        QTemporaryDir directory(m_root + QStringLiteral("/schema-XXXXXX"));
        {
            Worker worker;
            worker.initialize(directory.path());
            QTRY_COMPARE(worker.ready.count(), 1);
        }
        const QString path = directory.path() + QStringLiteral("/maintenance.db");
        {
            Database database(path);
            QVERIFY(database.exec(QStringLiteral("ALTER TABLE week_maintain ADD COLUMN alien TEXT")));
        }
        const QByteArray before = fileBytes(path);
        QVERIFY(!before.isEmpty());
        Worker worker;
        worker.initialize(directory.path());
        QTRY_COMPARE(worker.errors.count(), 1);
        QCOMPARE(worker.ready.count(), 0);
        QCOMPARE(fileBytes(path), before);
        worker.month(QDate::currentDate());
        QTRY_COMPARE(worker.errors.count(), 2);

        QTemporaryDir unrelated(m_root + QStringLiteral("/nonempty-XXXXXX"));
        QFile tag(unrelated.path() + QStringLiteral("/offline-data.tag"));
        QVERIFY(tag.open(QIODevice::WriteOnly));
        tag.write("SN100_INSTRUMENT_OFFLINE_V1\n");
        tag.close();
        const QString unrelatedPath = unrelated.path() + QStringLiteral("/maintenance.db");
        {
            Database database(unrelatedPath);
            // '_' in SQLite LIKE is a wildcard. A user table named sqliteX...
            // must not be mistaken for SQLite's internal sqlite_* namespace.
            QVERIFY(database.exec(QStringLiteral("CREATE TABLE sqliteXexisting (value TEXT)")));
        }
        const QByteArray unrelatedBefore = fileBytes(unrelatedPath);
        Worker nonempty;
        nonempty.initialize(unrelated.path());
        QTRY_COMPARE(nonempty.errors.count(), 1);
        QCOMPARE(nonempty.ready.count(), 0);
        QCOMPARE(fileBytes(unrelatedPath), unrelatedBefore);
    }

    void errorsForTamperedMarkersAndCorruptDatabases()
    {
        QTemporaryDir directory(m_root + QStringLiteral("/tamper-XXXXXX"));
        {
            Worker worker;
            worker.initialize(directory.path());
            QTRY_COMPARE(worker.ready.count(), 1);
        }
        const QString path = directory.path() + QStringLiteral("/maintenance.db");
        const QByteArray before = fileBytes(path);
        QFile tag(directory.path() + QStringLiteral("/offline-data.tag"));
        QVERIFY(tag.open(QIODevice::WriteOnly | QIODevice::Truncate));
        tag.write("invalid-marker");
        tag.close();
        Worker worker;
        worker.initialize(directory.path());
        QTRY_COMPARE(worker.errors.count(), 1);
        QCOMPARE(fileBytes(path), before);
        QVERIFY(tag.open(QIODevice::WriteOnly | QIODevice::Truncate));
        tag.write("SN100_INSTRUMENT_OFFLINE_V1\n");
        tag.close();
        QFile database(path);
        QVERIFY(database.open(QIODevice::WriteOnly | QIODevice::Truncate));
        database.write("not-an-sqlite-database");
        database.close();
        worker.initialize(directory.path());
        QTRY_COMPARE(worker.errors.count(), 2);
        QCOMPARE(fileBytes(path), QByteArray("not-an-sqlite-database"));
    }

    void neverPublishesPartialMonthAfterInvalidCompletionFlag()
    {
        QTemporaryDir directory(m_root + QStringLiteral("/bad-flag-XXXXXX"));
        Worker worker;
        worker.initialize(directory.path());
        QTRY_COMPARE(worker.ready.count(), 1);
        worker.session();
        worker.record(QStringLiteral("waste_cup"));
        worker.record(QStringLiteral("clean_pool"));
        QTRY_COMPARE(worker.saved.count(), 2);
        Database database(directory.path() + QStringLiteral("/maintenance.db"));
        QVERIFY(database.exec(QStringLiteral("UPDATE week_maintain SET clean_pool='invalid'")));
        worker.month(QDate::currentDate());
        QTRY_COMPARE(worker.errors.count(), 1);
        QCOMPARE(worker.loaded.count(), 0);
        QVERIFY(database.exec(QStringLiteral("UPDATE week_maintain SET clean_pool='1'")));
        worker.month(QDate::currentDate());
        QTRY_COMPARE(worker.loaded.count(), 1);
        QCOMPARE(qvariant_cast<MaintenanceRecords>(worker.loaded.last().at(0)).size(), 2);
    }

    void retriesTaggedEmptyDatabaseAfterInitializationLockFailure()
    {
        QTemporaryDir directory(m_root + QStringLiteral("/init-retry-XXXXXX"));
        QFile tag(directory.path() + QStringLiteral("/offline-data.tag"));
        QVERIFY(tag.open(QIODevice::WriteOnly));
        tag.write("SN100_INSTRUMENT_OFFLINE_V1\n");
        tag.close();
        // This reproduces the owned empty file left when first-time schema
        // creation fails before commit; it contains no user table to migrate.
        Database blocker(directory.path() + QStringLiteral("/maintenance.db"));
        QVERIFY(blocker.value.isOpen());
        QVERIFY(blocker.exec(QStringLiteral("BEGIN EXCLUSIVE")));
        Worker worker;
        worker.initialize(directory.path());
        QTRY_COMPARE(worker.errors.count(), 1);
        QCOMPARE(worker.ready.count(), 0);
        QVERIFY(blocker.exec(QStringLiteral("ROLLBACK")));
        worker.initialize(directory.path());
        QTRY_COMPARE(worker.ready.count(), 1);
        worker.session();
        worker.record(QStringLiteral("waste_cup"));
        QTRY_COMPARE(worker.saved.count(), 1);
        worker.month(QDate::currentDate());
        QTRY_COMPARE(worker.loaded.count(), 1);
        QCOMPARE(qvariant_cast<MaintenanceRecords>(worker.loaded.last().at(0)).size(), 1);
    }

    void monthUsesOneSnapshotAcrossConcurrentTableCommits()
    {
        QTemporaryDir directory(m_root + QStringLiteral("/snapshot-XXXXXX"));
        const QString path = directory.path() + QStringLiteral("/maintenance.db");
        Worker worker;
        worker.initialize(directory.path());
        QTRY_COMPARE(worker.ready.count(), 1);
        worker.session();
        for (const QString &action : {QStringLiteral("waste_cup"), QStringLiteral("waste_water"),
             QStringLiteral("waste_cup_box"), QStringLiteral("clean_pool"),
             QStringLiteral("clean_dirver"), QStringLiteral("clean_needle")})
            worker.record(action);
        QTRY_COMPARE(worker.saved.count(), 6);
        {
            Database database(path);
            QCOMPARE(database.scalar(QStringLiteral("PRAGMA journal_mode=WAL")).toString(), QStringLiteral("wal"));
        }
        SnapshotWriter writer(path);
        writer.start();
        QTRY_VERIFY(writer.committed.load() >= 5);
        const int requests = 100;
        for (int request = 0; request < requests; ++request)
            worker.month(QDate::currentDate());
        QTRY_COMPARE(worker.loaded.count(), requests);
        writer.stop();
        QVERIFY2(writer.failure.isEmpty(), qPrintable(writer.failure));
        QCOMPARE(worker.errors.count(), 0);
        for (const QList<QVariant> &snapshot : worker.loaded)
        {
            const int completions = qvariant_cast<MaintenanceRecords>(snapshot.at(0)).size();
            // The other connection updates all six flags atomically. A mixed
            // month (1..5) would prove table-to-table snapshot inconsistency.
            QVERIFY(completions == 0 || completions == 6);
        }
        m_measurements[QStringLiteral("consistent_month_snapshots")] = requests;
        m_measurements[QStringLiteral("concurrent_commits")] = writer.committed.load();
    }

    void cleanupTestCase()
    {
        QFile file(m_root + QStringLiteral("/measurements.json"));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray bytes = QJsonDocument(m_measurements).toJson();
        QCOMPARE(file.write(bytes), qint64(bytes.size()));
    }

private:
    QString m_root;
    QJsonObject m_measurements;
};

QTEST_GUILESS_MAIN(MaintenanceStoreTest)
#include "maintenance_store_test.moc"
