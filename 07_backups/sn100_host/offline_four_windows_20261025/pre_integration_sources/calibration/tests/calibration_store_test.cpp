#include "calibration/calibration_store.h"
#include <QtTest>
#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <limits>

namespace
{
QString testDirectoryTemplate()
{
    QString root = qEnvironmentVariable("CALIBRATION_TEST_ROOT");
    if (root.isEmpty()) root = QDir::current().filePath(QStringLiteral("calibration_test_runtime"));
    QDir().mkpath(root);
    return QDir(root).filePath(QStringLiteral("store-XXXXXX"));
}

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
    bool exec(const QString &sql)
    {
        QSqlQuery query(value);
        const bool success = query.exec(sql);
        if (!success) qWarning() << sql << query.lastError().text();
        return success;
    }
    QVariant scalar(const QString &sql)
    {
        QSqlQuery query(value);
        if (!query.exec(sql) || !query.next()) return QVariant();
        return query.value(0);
    }
    QString name;
    QSqlDatabase value;
};

class Worker
{
public:
    Worker() : store(new CalibrationStore), ready(store, &CalibrationStore::ready),
        loaded(store, &CalibrationStore::snapshotReady), catalog(store, &CalibrationStore::catalogReady),
        saved(store, &CalibrationStore::succeeded), errors(store, &CalibrationStore::error),
        closed(store, &CalibrationStore::closed)
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
    void initialize(const QString &directory, const QString &catalogPath)
    {
        QMetaObject::invokeMethod(store, "initialize", Qt::QueuedConnection,
                                 Q_ARG(QString, directory), Q_ARG(QString, catalogPath));
    }
    void session(int role = 2, bool authenticated = true, bool actionReady = true)
    {
        AccountSession session;
        session.name = QStringLiteral("anonymous-calibration-operator");
        session.authenticated = authenticated;
        session.actionReady = actionReady;
        session.role = role;
        session.source = AccountSessionSource::Simulated;
        QMetaObject::invokeMethod(store, "setSession", Qt::BlockingQueuedConnection, Q_ARG(AccountSession, session));
    }
    void save(const CalibrationRecord &record)
    {
        QMetaObject::invokeMethod(store, "save", Qt::QueuedConnection,
                                 Q_ARG(qint64, ++request), Q_ARG(CalibrationRecord, record));
    }
    CalibrationRecords load()
    {
        loaded.clear();
        QMetaObject::invokeMethod(store, "load", Qt::QueuedConnection, Q_ARG(qint64, ++request));
        if (loaded.isEmpty()) loaded.wait(3000);
        if (loaded.isEmpty()) return {};
        return qvariant_cast<CalibrationRecords>(loaded.last().at(1));
    }
    void copy(qint64 id, qint64 revision)
    {
        QMetaObject::invokeMethod(store, "copy", Qt::QueuedConnection,
                                 Q_ARG(qint64, ++request), Q_ARG(qint64, id), Q_ARG(qint64, revision));
    }
    void remove(qint64 id, qint64 revision)
    {
        QMetaObject::invokeMethod(store, "remove", Qt::QueuedConnection,
                                 Q_ARG(qint64, ++request), Q_ARG(qint64, id), Q_ARG(qint64, revision));
    }
    CalibrationStore *store;
    QThread thread;
    QSignalSpy ready, loaded, catalog, saved, errors, closed;
    qint64 request = 0;
};

struct Fixture
{
    Fixture() : directory(testDirectoryTemplate()), catalog(directory.path() + QStringLiteral("/catalog.db")) {}
    bool initialize()
    {
        const QStringList schema = {
            "CREATE TABLE catalog_metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL)",
            "INSERT INTO catalog_metadata VALUES('schema','sn100-catalog-offline-v1')",
            "CREATE TABLE option_unit(unit TEXT PRIMARY KEY,ratio REAL NOT NULL)",
            "INSERT INTO option_unit VALUES('mg/dL',1)",
            "INSERT INTO option_unit VALUES('Sec',1)",
            "CREATE TABLE project_base(id INTEGER PRIMARY KEY,project_id TEXT,project_en_name TEXT,project_cn_name TEXT,"
            "project_lis_code TEXT,is_used INTEGER,fixed INTEGER,change_time TEXT)",
            "INSERT INTO project_base VALUES(1,'offline-1','LOCAL_CAL','匿名校准项目','',1,0,'2026-10-25T00:00:00Z')",
            "CREATE TABLE project_result(project_id INTEGER,result_type TEXT,result_name TEXT,decimal_point INTEGER,"
            "test_low_range REAL,test_up_range REAL,normal_low_range REAL,normal_up_range REAL,lis_code TEXT,"
            "unit_name TEXT,main_result INTEGER)",
            "INSERT INTO project_result VALUES(1,'Calibration','浓度',2,0,99999,0,99999,'','mg/dL',0)",
            "INSERT INTO project_result VALUES(1,'Original','原始值',2,0,99999,0,99999,'','Sec',1)",
            "CREATE TABLE reagent_batch_group(id INTEGER PRIMARY KEY,project_id INTEGER,batch_code TEXT,batch_group TEXT)",
            "INSERT INTO reagent_batch_group VALUES(7,1,'ANON-BATCH','ANON-R1|ANON-R2')"
        };
        for (const QString &sql : schema) if (!catalog.exec(sql)) return false;
        worker.initialize(directory.path(), directory.path() + QStringLiteral("/catalog.db"));
        if (worker.ready.isEmpty() && !worker.ready.wait(3000)) return false;
        worker.session();
        return worker.ready.size() == 1;
    }
    CalibrationRecord record() const
    {
        CalibrationRecord record;
        record.projectId = 1;
        record.projectName = QStringLiteral("LOCAL_CAL");
        record.batchId = 7;
        record.batchCode = QStringLiteral("ANON-BATCH");
        record.bloodBatch = QStringLiteral("ANON-CAL-20261025");
        record.method = QStringLiteral("未拟合");
        record.xTransform = QStringLiteral("x");
        record.yTransform = QStringLiteral("y");
        record.concentrationUnit = QStringLiteral("mg/dL");
        record.resultUnit = QStringLiteral("Sec");
        record.points = {{1, QStringLiteral("1:1"), 100.125, {12.25, 12.75}},
                         {2, QStringLiteral("1:2"), 50, {18, 20}},
                         {3, QStringLiteral("1:4"), 25, {30, 32}}};
        return record;
    }
    QString localPath() const { return directory.path() + QStringLiteral("/calibration_local.db"); }
    QTemporaryDir directory;
    Database catalog;
    Worker worker;
};

QByteArray hashFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex();
}
}

class CalibrationStoreTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { CalibrationStore::registerTypes(); }

    void saveLoadReopenPreservesRawPoints()
    {
        Fixture fixture;
        QVERIFY(fixture.initialize());
        const QByteArray catalogBefore = hashFile(fixture.directory.path() + "/catalog.db");
        fixture.worker.save(fixture.record());
        QTRY_COMPARE(fixture.worker.saved.count(), 1);
        QCOMPARE(fixture.worker.errors.count(), 0);
        const CalibrationRecords records = fixture.worker.load();
        QCOMPARE(records.size(), 1);
        const CalibrationRecord record = records.first();
        QCOMPARE(record.revision, qint64(1));
        QCOMPARE(record.projectId, qint64(1));
        QCOMPARE(record.batchId, qint64(7));
        QCOMPARE(record.batchGroup, QStringLiteral("ANON-R1|ANON-R2"));
        QCOMPARE(record.points.size(), 3);
        QCOMPARE(record.points.at(0).concentration, 100.125);
        QCOMPARE(record.points.at(0).measurements, QList<double>({12.25, 12.75}));
        QCOMPARE(record.points.at(2).index, 3);
        QCOMPARE(record.concentrationUnit, QStringLiteral("mg/dL"));
        QCOMPARE(record.resultUnit, QStringLiteral("Sec"));
        QVERIFY(!record.createdAt.isEmpty());
        QVERIFY(QDateTime::fromString(record.createdAt, Qt::ISODateWithMs).isValid());
        QVERIFY(QDateTime::fromString(record.changedAt, Qt::ISODateWithMs).isValid());
        QCOMPARE(record.changedAt, record.createdAt);
        QMetaObject::invokeMethod(fixture.worker.store, "close", Qt::BlockingQueuedConnection);
        fixture.worker.ready.clear();
        fixture.worker.initialize(fixture.directory.path(), fixture.directory.path() + "/catalog.db");
        QTRY_COMPARE(fixture.worker.ready.count(), 1);
        const CalibrationRecords reopened = fixture.worker.load();
        QCOMPARE(reopened.size(), 1);
        QCOMPARE(reopened.first().id, record.id);
        QCOMPARE(reopened.first().points.at(0).concentration, 100.125);
        QCOMPARE(reopened.first().createdAt, record.createdAt);
        QCOMPARE(reopened.first().changedAt, record.changedAt);
        QCOMPARE(hashFile(fixture.directory.path() + "/catalog.db"), catalogBefore);
        Database local(fixture.localPath());
        QCOMPARE(local.scalar("SELECT is_checked+is_applied FROM calibration_records").toInt(), 0);
        QVERIFY(!local.exec("UPDATE calibration_records SET is_checked=1"));
        QVERIFY(!local.exec("UPDATE calibration_records SET is_applied=1"));
        qInfo() << "INPUT concentration mg/dL=100.125; measurements Sec=[12.25,12.75]; OUTPUT reopened exact, checked=0 applied=0";
    }

    void updateCopyUsesCommittedRevisionAndDeleteIsSoft()
    {
        Fixture fixture;
        QVERIFY(fixture.initialize());
        fixture.worker.save(fixture.record());
        QTRY_COMPARE(fixture.worker.saved.count(), 1);
        CalibrationRecord record = fixture.worker.load().first();
        const QString originalCreatedAt = record.createdAt;
        const QString originalChangedAt = record.changedAt;
        record.points.first().measurements = {11, 13};
        fixture.worker.save(record);
        QTRY_COMPARE(fixture.worker.saved.count(), 2);
        record = fixture.worker.load().first();
        QCOMPARE(record.revision, qint64(2));
        QCOMPARE(record.createdAt, originalCreatedAt);
        QVERIFY(QDateTime::fromString(record.changedAt, Qt::ISODateWithMs).isValid());
        QVERIFY(QDateTime::fromString(record.changedAt, Qt::ISODateWithMs)
                >= QDateTime::fromString(originalChangedAt, Qt::ISODateWithMs));
        fixture.worker.copy(record.id, record.revision);
        QTRY_COMPARE(fixture.worker.saved.count(), 3);
        CalibrationRecords records = fixture.worker.load();
        QCOMPARE(records.size(), 2);
        QVERIFY(records.at(0).id != records.at(1).id);
        QCOMPARE(records.at(1).sourceId, record.id);
        QCOMPARE(records.at(1).revision, qint64(1));
        QCOMPARE(records.at(1).createdAt, records.at(1).changedAt);
        QVERIFY(QDateTime::fromString(records.at(1).createdAt, Qt::ISODateWithMs).isValid());
        QCOMPARE(records.at(1).points.first().measurements, QList<double>({11, 13}));
        fixture.worker.remove(record.id, record.revision);
        QTRY_COMPARE(fixture.worker.saved.count(), 4);
        records = fixture.worker.load();
        QCOMPARE(records.size(), 1);
        Database local(fixture.localPath());
        QCOMPARE(local.scalar("SELECT count(*) FROM calibration_records").toInt(), 2);
        QCOMPARE(local.scalar("SELECT count(*) FROM calibration_points").toInt(), 6);
        QCOMPARE(local.scalar("SELECT visible FROM calibration_records WHERE id=1").toInt(), 0);
    }

    void validationRejectsEmptyIllegalAndDuplicates_data()
    {
        QTest::addColumn<int>("variant");
        for (int variant = 0; variant < 14; ++variant)
            QTest::newRow(qPrintable(QStringLiteral("invalid-%1").arg(variant))) << variant;
    }

    void validationRejectsEmptyIllegalAndDuplicates()
    {
        QFETCH(int, variant);
        Fixture fixture;
        QVERIFY(fixture.initialize());
        CalibrationRecord record = fixture.record();
        switch (variant)
        {
        case 0: record.points.clear(); break;
        case 1: record.points[1].concentration = record.points[0].concentration; break;
        case 2: record.points[0].concentration = -1; break;
        case 3: record.points[0].measurements[0] = std::numeric_limits<double>::infinity(); break;
        case 4: record.points[0].measurements.clear(); break;
        case 5: record.points[1].measurements.append(9); break;
        case 6: record.points[0].index = 0; break;
        case 7: record.points[0].sampleRate.clear(); break;
        case 8: record.bloodBatch.clear(); break;
        case 9: record.concentrationUnit.clear(); break;
        case 10: record.unitRatio = 0; break;
        case 11: record.points[0].measurements[0] = 100000; break;
        case 12: record.points[0].concentration = 1.12345; break;
        case 13: record.points[0].measurements[0] = 12.12345; break;
        }
        fixture.worker.save(record);
        QTRY_COMPARE(fixture.worker.errors.count(), 1);
        QCOMPARE(fixture.worker.saved.count(), 0);
        QCOMPARE(fixture.worker.load().size(), 0);
    }

    void permissionRejectedWithoutWrite_data()
    {
        QTest::addColumn<int>("role");
        QTest::addColumn<bool>("authenticated");
        QTest::addColumn<bool>("actionReady");
        QTest::newRow("guest") << 0 << false << true;
        QTest::newRow("invalid-role") << 6 << true << true;
        QTest::newRow("busy-session") << 2 << true << false;
    }

    void permissionRejectedWithoutWrite()
    {
        QFETCH(int, role);
        QFETCH(bool, authenticated);
        QFETCH(bool, actionReady);
        Fixture fixture;
        QVERIFY(fixture.initialize());
        fixture.worker.session(role, authenticated, actionReady);
        fixture.worker.save(fixture.record());
        QTRY_COMPARE(fixture.worker.errors.count(), 1);
        QCOMPARE(fixture.worker.saved.count(), 0);
        QCOMPARE(fixture.worker.load().size(), 0);
    }

    void staleRevisionAndRepeatedDeleteCannotOverwrite()
    {
        Fixture fixture;
        QVERIFY(fixture.initialize());
        fixture.worker.save(fixture.record());
        QTRY_COMPARE(fixture.worker.saved.count(), 1);
        CalibrationRecord record = fixture.worker.load().first();
        fixture.worker.save(record);
        QTRY_COMPARE(fixture.worker.saved.count(), 2);
        record.bloodBatch = QStringLiteral("STALE-EDIT");
        fixture.worker.save(record);
        QTRY_COMPARE(fixture.worker.errors.count(), 1);
        fixture.worker.copy(record.id, record.revision);
        QTRY_COMPARE(fixture.worker.errors.count(), 2);
        const CalibrationRecord current = fixture.worker.load().first();
        QCOMPARE(current.revision, qint64(2));
        QCOMPARE(current.bloodBatch, QStringLiteral("ANON-CAL-20261025"));
        fixture.worker.remove(current.id, current.revision);
        QTRY_COMPARE(fixture.worker.saved.count(), 3);
        fixture.worker.remove(current.id, current.revision);
        QTRY_COMPARE(fixture.worker.errors.count(), 3);
        QCOMPARE(fixture.worker.load().size(), 0);
    }

    void catalogRenameDeleteBatchAndUnitChangesRejectStaleWrites_data()
    {
        QTest::addColumn<QString>("mutation");
        QTest::newRow("rename-project") << QStringLiteral("UPDATE project_base SET project_en_name='RENAMED' WHERE id=1");
        QTest::newRow("delete-project") << QStringLiteral("DELETE FROM project_base WHERE id=1");
        QTest::newRow("disable-project") << QStringLiteral("UPDATE project_base SET is_used=0 WHERE id=1");
        QTest::newRow("delete-batch") << QStringLiteral("DELETE FROM reagent_batch_group WHERE id=7");
        QTest::newRow("rename-batch") << QStringLiteral("UPDATE reagent_batch_group SET batch_code='CHANGED' WHERE id=7");
        QTest::newRow("reassign-batch") << QStringLiteral("UPDATE reagent_batch_group SET project_id=2 WHERE id=7");
        QTest::newRow("change-batch-components") << QStringLiteral("UPDATE reagent_batch_group SET batch_group='CHANGED-R1|ANON-R2' WHERE id=7");
        QTest::newRow("unit-ratio") << QStringLiteral("UPDATE option_unit SET ratio=2 WHERE unit='mg/dL'");
        QTest::newRow("remove-calibration-result") << QStringLiteral("DELETE FROM project_result WHERE result_type='Calibration'");
        QTest::newRow("remove-concentration-unit") << QStringLiteral("DELETE FROM option_unit WHERE unit='mg/dL'");
        QTest::newRow("rename-original-result-unit") << QStringLiteral("UPDATE project_result SET unit_name='mg/dL' WHERE result_type='Original'");
        QTest::newRow("remove-original-result") << QStringLiteral("DELETE FROM project_result WHERE result_type='Original'");
        QTest::newRow("remove-original-unit") << QStringLiteral("DELETE FROM option_unit WHERE unit='Sec'");
    }

    void catalogRenameDeleteBatchAndUnitChangesRejectStaleWrites()
    {
        QFETCH(QString, mutation);
        Fixture fixture;
        QVERIFY(fixture.initialize());
        fixture.worker.save(fixture.record());
        QTRY_COMPARE(fixture.worker.saved.count(), 1);
        CalibrationRecord record = fixture.worker.load().first();
        QVERIFY(fixture.catalog.exec(mutation));
        fixture.worker.save(record);
        QTRY_COMPARE(fixture.worker.errors.count(), 1);
        fixture.worker.copy(record.id, record.revision);
        QTRY_COMPARE(fixture.worker.errors.count(), 2);
        const CalibrationRecords history = fixture.worker.load();
        QCOMPARE(history.size(), 1);
        QCOMPARE(history.first().projectName, QStringLiteral("LOCAL_CAL"));
        QCOMPARE(history.first().batchCode, QStringLiteral("ANON-BATCH"));
        QCOMPARE(history.first().unitRatio, 1.0);
        QCOMPARE(history.first().revision, qint64(1));
    }

    void lockFailurePreservesDataAndRetryWorks()
    {
        Fixture fixture;
        QVERIFY(fixture.initialize());
        fixture.worker.save(fixture.record());
        QTRY_COMPARE(fixture.worker.saved.count(), 1);
        CalibrationRecord record = fixture.worker.load().first();
        Database lock(fixture.localPath());
        QVERIFY(lock.exec("BEGIN IMMEDIATE"));
        record.points[0].measurements = {40, 42};
        fixture.worker.save(record);
        QTRY_COMPARE(fixture.worker.errors.count(), 1);
        QCOMPARE(fixture.worker.saved.count(), 1);
        QVERIFY(lock.exec("ROLLBACK"));
        QCOMPARE(fixture.worker.load().first().points.first().measurements, QList<double>({12.25, 12.75}));
        fixture.worker.save(record);
        QTRY_COMPARE(fixture.worker.saved.count(), 2);
        QCOMPARE(fixture.worker.load().first().points.first().measurements, QList<double>({40, 42}));
        qInfo() << "INPUT BEGIN IMMEDIATE competing writer; OUTPUT save error, unchanged values; rollback lock then explicit retry succeeds";
    }

    void latePointFailureRollsBackHeaderAndAllPoints()
    {
        Fixture fixture;
        QVERIFY(fixture.initialize());
        fixture.worker.save(fixture.record());
        QTRY_COMPARE(fixture.worker.saved.count(), 1);
        CalibrationRecord record = fixture.worker.load().first();
        Database injection(fixture.localPath());
        QVERIFY(injection.exec("CREATE TRIGGER injected_point_failure BEFORE INSERT ON calibration_points "
                               "WHEN NEW.point_index=2 BEGIN SELECT RAISE(ABORT,'calibration injected point failure'); END"));
        record.points[0].measurements = {66, 68};
        fixture.worker.save(record);
        QTRY_COMPARE(fixture.worker.errors.count(), 1);
        QVERIFY(fixture.worker.errors.first().at(2).toString().contains(QStringLiteral("calibration injected point failure")));
        QCOMPARE(fixture.worker.saved.count(), 1);
        CalibrationRecords records = fixture.worker.load();
        QCOMPARE(records.size(), 1);
        QCOMPARE(records.first().bloodBatch, QStringLiteral("ANON-CAL-20261025"));
        QCOMPARE(records.first().revision, qint64(1));
        QCOMPARE(records.first().createdAt, record.createdAt);
        QCOMPARE(records.first().changedAt, record.changedAt);
        QCOMPARE(records.first().points.size(), 3);
        QCOMPARE(records.first().points.first().measurements, QList<double>({12.25, 12.75}));
        QVERIFY(injection.exec("DROP TRIGGER injected_point_failure"));
        fixture.worker.save(record);
        QTRY_COMPARE(fixture.worker.saved.count(), 2);
        const CalibrationRecord retried = fixture.worker.load().first();
        QCOMPARE(retried.bloodBatch, QStringLiteral("ANON-CAL-20261025"));
        QCOMPARE(retried.revision, qint64(2));
        QCOMPARE(retried.createdAt, record.createdAt);
        QVERIFY(QDateTime::fromString(retried.changedAt, Qt::ISODateWithMs)
                >= QDateTime::fromString(record.changedAt, Qt::ISODateWithMs));
        QCOMPARE(retried.points.first().measurements, QList<double>({66, 68}));
        qInfo() << "INPUT late INSERT trigger failure on point2; OUTPUT revision/header/point1 all rolled back; explicit retry after trigger removal succeeds";
    }

    void catalogSnapshotIsCommittedReadOnlyAndStableIds()
    {
        Fixture fixture;
        QVERIFY(fixture.initialize());
        const QByteArray before = hashFile(fixture.directory.path() + "/catalog.db");
        QMetaObject::invokeMethod(fixture.worker.store, "loadCatalog", Qt::QueuedConnection, Q_ARG(qint64, qint64(101)));
        QTRY_COMPARE(fixture.worker.catalog.count(), 1);
        const auto payload = fixture.worker.catalog.first();
        QCOMPARE(payload.at(0).toLongLong(), qint64(101));
        const ProjectRecords projects = qvariant_cast<ProjectRecords>(payload.at(1));
        const ProjectUnits units = qvariant_cast<ProjectUnits>(payload.at(2));
        const ReagentSnapshot reagents = qvariant_cast<ReagentSnapshot>(payload.at(3));
        QCOMPARE(projects.size(), 1);
        QCOMPARE(projects.first().id, qint64(1));
        QCOMPARE(projects.first().results.size(), 2);
        QCOMPARE(units.size(), 2);
        QCOMPARE(reagents.batches.size(), 1);
        QCOMPARE(reagents.batches.first().id, qint64(7));
        QCOMPARE(hashFile(fixture.directory.path() + "/catalog.db"), before);
    }

    void zeroAndDescendingConcentrationsRemainInInputOrder()
    {
        Fixture fixture;
        QVERIFY(fixture.initialize());
        CalibrationRecord record = fixture.record();
        record.points[2].concentration = 0;
        fixture.worker.save(record);
        QTRY_COMPARE(fixture.worker.saved.count(), 1);
        const CalibrationRecord saved = fixture.worker.load().first();
        QCOMPARE(saved.points[0].concentration, 100.125);
        QCOMPARE(saved.points[1].concentration, 50.0);
        QCOMPARE(saved.points[2].concentration, 0.0);
        QCOMPARE(saved.points[2].index, 3);
    }

    void unboundBatchStoresExplicitEmptyString()
    {
        Fixture fixture;
        QVERIFY(fixture.initialize());
        CalibrationRecord record = fixture.record();
        record.batchId = 0;
        record.batchCode = QString();
        fixture.worker.save(record);
        QTRY_COMPARE(fixture.worker.saved.count(), 1);
        QCOMPARE(fixture.worker.errors.count(), 0);
        const CalibrationRecords records = fixture.worker.load();
        QCOMPARE(records.size(), 1);
        QCOMPARE(records.first().batchId, qint64(0));
        QVERIFY(records.first().batchCode.isEmpty());
        Database local(fixture.localPath());
        QCOMPARE(local.scalar("SELECT batch_code IS NULL FROM calibration_records").toInt(), 0);
        QCOMPARE(local.scalar("SELECT batch_group IS NULL FROM calibration_records").toInt(), 0);
    }

    void savedIdentityCannotBeReassociated_data()
    {
        QTest::addColumn<int>("variant");
        QTest::newRow("another-live-project") << 0;
        QTest::newRow("another-live-batch") << 1;
        QTest::newRow("changed-current-concentration-unit") << 2;
        QTest::newRow("changed-current-original-unit") << 3;
        QTest::newRow("different-method") << 4;
        QTest::newRow("different-transform") << 5;
        QTest::newRow("different-calibrator-batch") << 6;
        QTest::newRow("fabricated-copy-source") << 7;
    }

    void savedIdentityCannotBeReassociated()
    {
        QFETCH(int, variant);
        Fixture fixture;
        QVERIFY(fixture.initialize());
        fixture.worker.save(fixture.record());
        QTRY_COMPARE(fixture.worker.saved.count(), 1);
        CalibrationRecord record = fixture.worker.load().first();
        switch (variant)
        {
        case 0:
            QVERIFY(fixture.catalog.exec("INSERT INTO project_base VALUES(2,'offline-2','ANOTHER_CAL','匿名项目2','',1,0,'2026-10-25T00:00:00Z')"));
            QVERIFY(fixture.catalog.exec("INSERT INTO project_result SELECT 2,result_type,result_name,decimal_point,"
                "test_low_range,test_up_range,normal_low_range,normal_up_range,lis_code,unit_name,main_result "
                "FROM project_result WHERE project_id=1"));
            record.projectId = 2;
            record.projectName = QStringLiteral("ANOTHER_CAL");
            record.batchId = 0;
            record.batchCode.clear();
            record.batchGroup.clear();
            break;
        case 1:
            QVERIFY(fixture.catalog.exec("INSERT INTO reagent_batch_group VALUES(8,1,'ANOTHER-BATCH','R3|R4')"));
            record.batchId = 8;
            record.batchCode = QStringLiteral("ANOTHER-BATCH");
            record.batchGroup = QStringLiteral("R3|R4");
            break;
        case 2:
            QVERIFY(fixture.catalog.exec("INSERT INTO option_unit VALUES('g/L',10)"));
            QVERIFY(fixture.catalog.exec("UPDATE project_result SET unit_name='g/L' WHERE result_type='Calibration'"));
            record.concentrationUnit = QStringLiteral("g/L");
            record.unitRatio = 10;
            break;
        case 3:
            QVERIFY(fixture.catalog.exec("INSERT INTO option_unit VALUES('min',1)"));
            QVERIFY(fixture.catalog.exec("UPDATE project_result SET unit_name='min' WHERE result_type='Original'"));
            record.resultUnit = QStringLiteral("min");
            break;
        case 4: record.method = QStringLiteral("different-method"); break;
        case 5: record.xTransform = QStringLiteral("log(x)"); break;
        case 6: record.bloodBatch = QStringLiteral("different-calibrator"); break;
        case 7: record.sourceId = 999; break;
        }
        fixture.worker.save(record);
        QTRY_COMPARE(fixture.worker.errors.count(), 1);
        QVERIFY(fixture.worker.errors.first().at(2).toString().contains(QStringLiteral("身份不可改写")));
        QCOMPARE(fixture.worker.saved.count(), 1);
        const CalibrationRecord unchanged = fixture.worker.load().first();
        QCOMPARE(unchanged.projectId, qint64(1));
        QCOMPARE(unchanged.batchId, qint64(7));
        QCOMPARE(unchanged.batchGroup, QStringLiteral("ANON-R1|ANON-R2"));
        QCOMPARE(unchanged.concentrationUnit, QStringLiteral("mg/dL"));
        QCOMPARE(unchanged.resultUnit, QStringLiteral("Sec"));
        QCOMPARE(unchanged.bloodBatch, QStringLiteral("ANON-CAL-20261025"));
        QCOMPARE(unchanged.revision, qint64(1));
    }

    void unknownExistingFileAndReferencePathsAreRejected()
    {
        Fixture fixture;
        QVERIFY(fixture.initialize());
        QTemporaryDir other(testDirectoryTemplate());
        const QString unknownPath = other.path() + "/calibration_local.db";
        {
            Database unknown(unknownPath);
            QVERIFY(unknown.exec("CREATE TABLE unique_original(value TEXT)"));
            QVERIFY(unknown.exec("INSERT INTO unique_original VALUES('keep')"));
        }
        const QByteArray before = hashFile(unknownPath);
        fixture.worker.initialize(other.path(), fixture.directory.path() + "/catalog.db");
        QTRY_COMPARE(fixture.worker.errors.count(), 1);
        QCOMPARE(hashFile(unknownPath), before);
        fixture.worker.initialize(other.path() + "/sn100_host_reference", fixture.directory.path() + "/catalog.db");
        QTRY_COMPARE(fixture.worker.errors.count(), 2);
        QVERIFY(!QFileInfo::exists(other.path() + "/sn100_host_reference"));
    }

    void wrongThreadAndClosedStoreCannotAccessSql()
    {
        Fixture fixture;
        QVERIFY(fixture.initialize());
        // Deliberately call directly from test thread: fail before touching SQL.
        fixture.worker.store->save(999, fixture.record());
        QTRY_COMPARE(fixture.worker.errors.count(), 1);
        QCOMPARE(fixture.worker.load().size(), 0);
        QMetaObject::invokeMethod(fixture.worker.store, "close", Qt::BlockingQueuedConnection);
        fixture.worker.save(fixture.record());
        QTRY_COMPARE(fixture.worker.errors.count(), 2);
        QCOMPARE(fixture.worker.saved.count(), 0);
        QVERIFY(fixture.worker.thread.isRunning());
    }
};

QTEST_GUILESS_MAIN(CalibrationStoreTest)
#include "calibration_store_test.moc"
