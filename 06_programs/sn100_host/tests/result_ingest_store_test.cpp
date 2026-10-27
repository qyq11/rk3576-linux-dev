#include "../history/history_store.h"
#include "../projects/projects_store.h"

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QThread>
#include <QUuid>
#include <QVariant>
#include <functional>
#include <limits>

namespace
{
QString testRoot;

void require(bool condition, const QString &message)
{
    // Infrastructure errors abort the test process; they never become passes.
    if (!condition) qFatal("%s", qPrintable(message));
}

bool symlinkComponent(QString path)
{
    for (;;)
    {
        const QFileInfo info(path);
        if (info.isSymLink()) return true;
        const QString parent = info.dir().absolutePath();
        if (parent == path) return false;
        path = parent;
    }
}

AccountSession seedEngineer()
{
    AccountSession session;
    session.name = QStringLiteral("anon_seed_engineer");
    session.role = 4;
    session.authenticated = true;
    session.actionReady = true;
    session.source = AccountSessionSource::Simulated;
    return session;
}

AccountSession ordinaryOperator(bool actionReady = true)
{
    AccountSession session;
    session.name = QStringLiteral("anon_result_operator");
    session.role = 1;
    session.authenticated = true;
    session.actionReady = actionReady;
    session.source = AccountSessionSource::Simulated;
    return session;
}

QDateTime referenceTime()
{
    return QDateTime(QDate(2026, 10, 26), QTime(9, 30, 0));
}

HistoryFilter allReports()
{
    HistoryFilter filter;
    filter.start = referenceTime().addDays(-1);
    filter.end = referenceTime().addDays(1);
    filter.limit = 100;
    return filter;
}

class TestSql
{
public:
    explicit TestSql(const QString &path, bool readOnly = true)
    {
        require(path.startsWith(testRoot + QChar('/')) && QFileInfo(path).isFile()
                    && !symlinkComponent(path), QStringLiteral("SQL path is not an existing owned test file: ") + path);
        name_ = QStringLiteral("result-test-inspection-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name_);
        database_.setDatabaseName(path);
        database_.setConnectOptions(readOnly ? QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=200")
                                            : QStringLiteral("QSQLITE_BUSY_TIMEOUT=200"));
        require(database_.open(), database_.lastError().text());
    }
    ~TestSql()
    {
        database_.close();
        database_ = QSqlDatabase();
        QSqlDatabase::removeDatabase(name_);
    }
    QSqlDatabase &database() { return database_; }
private:
    Q_DISABLE_COPY(TestSql)
    QString name_;
    QSqlDatabase database_;
};

using Rows = QList<QVariantList>;

Rows selectRows(QSqlDatabase database, const QString &sql, const QVariantList &bindings = {})
{
    QSqlQuery query(database);
    require(query.prepare(sql), query.lastError().text());
    for (const auto &value : bindings) query.addBindValue(value);
    require(query.exec(), query.lastError().text());
    Rows rows;
    while (query.next())
    {
        QVariantList row;
        for (int index = 0; index < query.record().count(); ++index) row.append(query.value(index));
        rows.append(row);
    }
    require(!query.lastError().isValid(), query.lastError().text());
    return rows;
}

Rows selectRows(const QString &path, const QString &sql, const QVariantList &bindings = {})
{
    TestSql connection(path);
    return selectRows(connection.database(), sql, bindings);
}

QVariant scalar(const QString &path, const QString &sql, const QVariantList &bindings = {})
{
    const auto rows = selectRows(path, sql, bindings);
    require(rows.size() == 1 && rows.first().size() == 1, QStringLiteral("Expected exactly one scalar result: ") + sql);
    return rows.first().first();
}

QByteArray encoded(const Rows &rows)
{
    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_12);
    stream << rows;
    require(stream.status() == QDataStream::Ok, QStringLiteral("Snapshot serialization failed"));
    return result;
}

QByteArray logicalSnapshot(const QString &path)
{
    // Compare committed schema and all logical rows, not SQLite file bytes or
    // filesystem timestamps (successful hold/release can legitimately change pages).
    TestSql connection(path);
    QSqlDatabase &database = connection.database();
    require(database.transaction(), database.lastError().text());
    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_12);
    stream << selectRows(database, QStringLiteral("PRAGMA application_id"))
           << selectRows(database, QStringLiteral("PRAGMA user_version"))
           << selectRows(database, QStringLiteral("SELECT type,name,tbl_name,sql FROM sqlite_master ORDER BY type,name"));
    const auto tables = selectRows(database, QStringLiteral("SELECT name FROM sqlite_master WHERE type='table' ORDER BY name"));
    for (const auto &row : tables)
    {
        QString table = row.first().toString();
        table.replace(QChar('"'), QStringLiteral("\"\""));
        stream << table << selectRows(database, QStringLiteral("SELECT * FROM \"") + table + QStringLiteral("\" ORDER BY rowid"));
    }
    require(stream.status() == QDataStream::Ok, QStringLiteral("Database snapshot serialization failed"));
    require(database.commit(), database.lastError().text());
    return result;
}

void executeOwnedDdl(const QString &path, const QString &sql)
{
    TestSql connection(path, false);
    QSqlQuery query(connection.database());
    require(query.exec(QStringLiteral("PRAGMA foreign_keys=ON")), query.lastError().text());
    require(query.exec(sql), query.lastError().text());
}

struct StoreError { qint64 id; QString operation; QString message; };
struct ReservationEvent { qint64 id; HistoryIds reportIds; };
struct CompletionEvent { qint64 id; HistoryComputedResult result; };
struct QueryEvent { qint64 id; HistoryReports reports; qint64 total; };
struct Events
{
    int readyCount = 0;
    int closedCount = 0;
    int displayCount = 0;
    bool open = false;
    QString databasePath;
    QList<StoreError> errors;
    QList<ReservationEvent> reservations;
    QList<CompletionEvent> completions;
    QList<QueryEvent> queries;
    QList<HistoryReport> details;
    QList<HistoryPatient> patients;
    QList<HistoryIds> hidden;
    HistoryDisplayColumns display;
};

struct CatalogReply
{
    int readyCount = 0;
    int successCount = 0;
    int referenceCount = 0;
    bool exists = false;
    qint64 projectId = 0;
    QStringList references;
    QStringList errors;
    ProjectRecords projects;
};

class Fixture
{
public:
    Fixture(const QString &caseName, const AccountSession &businessSession)
    {
        caseDirectory = QDir(testRoot).filePath(caseName);
        require(!QFileInfo::exists(caseDirectory) && QDir().mkpath(caseDirectory), QStringLiteral("Test case directory must be new"));
        caseDirectory = QFileInfo(caseDirectory).canonicalFilePath();
        catalogDirectory = QDir(caseDirectory).filePath(QStringLiteral("catalog"));
        historyDirectory = QDir(caseDirectory).filePath(QStringLiteral("history"));
        catalogPath = QDir(catalogDirectory).filePath(QStringLiteral("catalog.db"));
        historyPath = QDir(historyDirectory).filePath(QStringLiteral("history.sqlite"));
        context_ = new QObject;
        context_->moveToThread(&thread_);
        QObject::connect(&thread_, &QThread::finished, context_, &QObject::deleteLater);
        thread_.start();

        // Separate setup actor, explicit engineer session. The HistoryStore
        // below receives only the business session supplied by each test.
        const auto seed = catalogCall(seedEngineer(), true, [](ProjectsStore &store) { store.load(11); });
        require(seed.readyCount == 1 && seed.errors.isEmpty(), QStringLiteral("Actual catalog setup failed: ") + seed.errors.join(QChar(';')));
        for (const auto &record : seed.projects)
            if (record.name == QStringLiteral("DEMO_A")) project = record;
        require(project.id > 0 && project.enabled && project.instrumentProjectId == QStringLiteral("OFFLINE-001"),
                QStringLiteral("Actual seeded project identity was not returned"));
        require(project.name.size() <= 16 && project.instrumentProjectId.size() <= 128,
                QStringLiteral("Project setup exceeds actual source bounds"));
        bool haveRate = false;
        for (const auto &rate : project.sampleRates)
            if (rate.sampleRate == QStringLiteral("OFFLINE_RATE_A") && rate.sampleOption) haveRate = true;
        require(haveRate, QStringLiteral("Actual project sample-rate prerequisite missing"));
        inWorker([&]() {
            store_ = new HistoryStore(context_);
            QObject::connect(store_, &HistoryStore::ready, context_, [&](qint64 id, const QString &path) {
                require(id == 0, QStringLiteral("Unexpected lifecycle request id"));
                ++events.readyCount; events.open = true; events.databasePath = path;
            }, Qt::DirectConnection);
            QObject::connect(store_, &HistoryStore::error, context_, [&](qint64 id, const QString &operation, const QString &message) {
                events.errors.append({id, operation, message});
            }, Qt::DirectConnection);
            QObject::connect(store_, &HistoryStore::closed, context_, [&]() {
                ++events.closedCount; events.open = false;
            }, Qt::DirectConnection);
            QObject::connect(store_, &HistoryStore::localTasksReserved, context_, [&](qint64 id, HistoryIds ids) {
                events.reservations.append({id, ids});
            }, Qt::DirectConnection);
            QObject::connect(store_, &HistoryStore::computedResultCommitted, context_, [&](qint64 id, HistoryComputedResult value) {
                events.completions.append({id, value});
            }, Qt::DirectConnection);
            QObject::connect(store_, &HistoryStore::queryReady, context_, [&](qint64 id, HistoryFilter, HistoryReports reports, qint64 total) {
                events.queries.append({id, reports, total});
            }, Qt::DirectConnection);
            QObject::connect(store_, &HistoryStore::detailReady, context_, [&](qint64, HistoryReport report) {
                events.details.append(report);
            }, Qt::DirectConnection);
            QObject::connect(store_, &HistoryStore::patientSaved, context_, [&](qint64, HistoryPatient patient) {
                events.patients.append(patient);
            }, Qt::DirectConnection);
            QObject::connect(store_, &HistoryStore::reportsHidden, context_, [&](qint64, HistoryIds ids) {
                events.hidden.append(ids);
            }, Qt::DirectConnection);
            QObject::connect(store_, &HistoryStore::displayLoaded, context_, [&](qint64, HistoryDisplayColumns columns, HistoryDisplayColumns) {
                ++events.displayCount; events.display = columns;
            }, Qt::DirectConnection);
            store_->setSession(businessSession);
        });
        open();
    }
    ~Fixture()
    {
        inWorker([&]() {
            if (events.open) store_->close();
            delete store_; store_ = nullptr;
        });
        thread_.quit();
        require(thread_.wait(5000), QStringLiteral("Database worker did not stop within 5000ms"));
        // Preserve runtime files as evidence; no recursive removal or hidden rerun.
    }

    void inWorker(const std::function<void()> &function)
    {
        require(QMetaObject::invokeMethod(context_, function, Qt::BlockingQueuedConnection), QStringLiteral("Worker invocation failed"));
    }
    void open()
    {
        inWorker([&]() { events.open = false; store_->initializeLocalResults(historyDirectory, catalogDirectory); });
    }
    void close()
    {
        inWorker([&]() { if (events.open) store_->close(); });
    }
    void session(const AccountSession &value)
    {
        inWorker([&]() { store_->setSession(value); });
    }
    qint64 reserve(const HistoryReservationBatch &batch)
    {
        const qint64 id = ++serial_;
        inWorker([&]() { store_->reserveLocalTasks(id, batch); });
        return id;
    }
    qint64 complete(const HistoryComputedResult &value)
    {
        const qint64 id = ++serial_;
        inWorker([&]() { store_->commitComputedResult(id, value); });
        return id;
    }
    void query()
    {
        const qint64 id = ++serial_;
        inWorker([&]() { store_->query(id, allReports()); });
    }
    void detail(qint64 reportId)
    {
        const qint64 id = ++serial_;
        inWorker([&]() { store_->detail(id, reportId); });
    }
    void savePatient(const HistoryPatient &patient)
    {
        const qint64 id = ++serial_;
        inWorker([&]() { store_->savePatient(id, patient); });
    }
    void hide(qint64 reportId)
    {
        const qint64 id = ++serial_;
        inWorker([&]() { store_->hideReports(id, HistoryIds{reportId}); });
    }
    void loadDisplay()
    {
        const qint64 id = ++serial_;
        inWorker([&]() { store_->loadDisplay(id); });
    }
    int exportCsv(qint64 requestId, const QString &path)
    {
        int succeeded = 0;
        inWorker([&]() {
            const auto connection = QObject::connect(store_, &HistoryStore::csvExported, context_,
                [&](qint64, const QString &, qint64, qint64) { ++succeeded; }, Qt::DirectConnection);
            store_->exportCsv(requestId, allReports(), HistoryIds(), path);
            QObject::disconnect(connection);
        });
        return succeeded;
    }
    CatalogReply references()
    {
        return catalogCall(AccountSession(), false, [&](ProjectsStore &store) { store.checkProjectReferences(12, project.id); });
    }
    CatalogReply releaseAsExplicitOperator(const QString &key)
    {
        return catalogCall(ordinaryOperator(), false, [&](ProjectsStore &store) {
            store.releaseProjectReference(13, QStringLiteral("history_results"), key);
        });
    }
    QString storeId() const
    {
        return scalar(historyPath, QStringLiteral("SELECT value FROM result_ingest_metadata WHERE key='store_id'")).toString();
    }
    QString key(qint64 taskId) const { return storeId() + QChar(':') + QString::number(taskId); }
    QByteArray reportData() const { return encoded(selectRows(historyPath, QStringLiteral("SELECT * FROM report_data ORDER BY id"))); }
    QStringList values(qint64 taskId) const
    {
        QStringList result;
        for (const auto &row : selectRows(historyPath, QStringLiteral("SELECT raw_value FROM report_values WHERE task_id=? ORDER BY result_type"), {taskId}))
            result.append(row.first().toString());
        return result;
    }
    qint64 count(const QString &table) const
    {
        const QStringList allowed{"report_data", "report_results", "report_values", "report_columns", "result_task_identity"};
        require(allowed.contains(table), QStringLiteral("Unexpected table identifier"));
        return scalar(historyPath, QStringLiteral("SELECT COUNT(*) FROM ") + table).toLongLong();
    }

    Events events;
    ProjectRecord project;
    QString caseDirectory, catalogDirectory, historyDirectory, catalogPath, historyPath;
private:
    CatalogReply catalogCall(const AccountSession &session, bool seedDemo, const std::function<void(ProjectsStore &)> &action)
    {
        CatalogReply reply;
        inWorker([&]() {
            ProjectsStore store;
            QObject::connect(&store, &ProjectsStore::ready, &store, [&](const QString &) { ++reply.readyCount; }, Qt::DirectConnection);
            QObject::connect(&store, &ProjectsStore::error, &store, [&](qint64, const QString &operation, const QString &message) {
                reply.errors.append(operation + QChar(':') + message);
            }, Qt::DirectConnection);
            QObject::connect(&store, &ProjectsStore::projectReferenceError, &store, [&](qint64, const QString &operation, const QString &message) {
                reply.errors.append(operation + QChar(':') + message);
            }, Qt::DirectConnection);
            QObject::connect(&store, &ProjectsStore::snapshotReady, &store, [&](qint64, ProjectRecords projects, ProjectUnits) {
                reply.projects = projects;
            }, Qt::DirectConnection);
            QObject::connect(&store, &ProjectsStore::projectReferenceSucceeded, &store, [&](qint64, const QString &) {
                ++reply.successCount;
            }, Qt::DirectConnection);
            QObject::connect(&store, &ProjectsStore::projectReferencesReady, &store, [&](qint64, qint64 projectId, bool exists, QStringList refs) {
                ++reply.referenceCount; reply.projectId = projectId; reply.exists = exists; reply.references = refs;
            }, Qt::DirectConnection);
            store.setSession(session);
            store.initialize(catalogDirectory, seedDemo);
            if (reply.readyCount == 1 && reply.errors.isEmpty()) action(store);
            store.close();
        });
        return reply;
    }
    Q_DISABLE_COPY(Fixture)
    QThread thread_;
    QObject *context_ = nullptr;
    HistoryStore *store_ = nullptr;
    qint64 serial_ = 100;
};

HistoryReservationBatch batchFor(const Fixture &fixture, qint64 rowId = 101, qint64 firstTask = 1001)
{
    HistoryReportReservation group;
    group.report.rowId = rowId;
    group.report.sampleNumber = 1;
    group.report.barcode = QStringLiteral("ANONBC001");
    group.report.dateTime = referenceTime();
    group.report.patient.name = QStringLiteral("ANON00001");
    group.report.patient.sex = -1;
    group.report.patient.age = 21;
    group.automaticNumber = true;
    group.sampleHole = QStringLiteral("1");
    HistoryReservationBatch batch;
    batch.reports.append(group);
    for (int index = 0; index < 2; ++index)
    {
        HistoryTaskReservation task;
        task.taskType = QStringLiteral("SAM");
        task.taskId = firstTask + index;
        task.reportRowId = rowId;
        task.projectId = fixture.project.id;
        task.instrumentProjectId = fixture.project.instrumentProjectId;
        task.projectName = fixture.project.name;
        task.sampleId = group.report.barcode;
        task.dilution = QStringLiteral("OFFLINE_RATE_A");
        task.reservedAt = referenceTime();
        require(task.taskId > 0 && task.taskId <= std::numeric_limits<qint32>::max()
                    && rowId > 0 && rowId <= std::numeric_limits<qint32>::max()
                    && task.projectName.size() <= 128 && task.instrumentProjectId.size() <= 128
                    && task.sampleId.size() <= 32767 && task.dilution.size() <= 4096
                    && task.reservedAt.isValid(), QStringLiteral("Test inputs exceed actual reservation contract"));
        batch.tasks.append(task);
    }
    return batch;
}

HistoryComputedResult resultFor(const HistoryTaskReservation &task, int variant = 0)
{
    HistoryComputedResult result;
    result.taskType = task.taskType;
    result.mdaName = task.mdaName;
    result.taskId = task.taskId;
    result.reportRowId = task.reportRowId;
    result.projectId = task.projectId;
    result.instrumentProjectId = task.instrumentProjectId;
    result.projectName = task.projectName;
    result.sampleId = task.sampleId;
    result.dilution = task.dilution;
    result.calibrationId = variant == 0 ? QStringLiteral("ANONCAL1") : QStringLiteral("ANONCAL2");
    result.abnormalType = variant == 0 ? QStringLiteral("4") : QStringLiteral("0");
    result.testedAt = referenceTime().addSecs(10 + variant);
    result.original = variant == 0 ? QStringLiteral("12.34000000") : QStringLiteral("raw+changed");
    result.calibration = variant == 0 ? QStringLiteral("") : QStringLiteral("000.50000");
    result.rate = variant == 0 ? QStringLiteral("****") : QStringLiteral("-text-");
    result.inr = variant == 0 ? QStringLiteral("0.00125000") : QStringLiteral("9.00000000");
    for (const auto &text : {result.original, result.calibration, result.rate, result.inr, result.calibrationId, result.abnormalType})
        require(text.size() <= 4096 && !text.contains(QChar::Null), QStringLiteral("Test result exceeds actual input contract"));
    return result;
}

QStringList fourValues(const HistoryComputedResult &result)
{
    return {result.original, result.calibration, result.rate, result.inr};
}
} // namespace

class ResultIngestCoreTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void emptyProfileHasNoFixture();
    void twoTasksKeepFourRawStringsAndUnknownUnits();
    void repeatedCompletionPreservesPatientAndHiddenReport();
    void retestAndResetHaveDifferentIdentitySemantics();
    void realAbortRollsBackDataAndOnlyNewHolds();
    void reopenChecksActualReferencesAndRejectsWrongIdentity();
    void localResultCsvExportIsBlockedUntilUnitsAreMapped();
};

void ResultIngestCoreTest::initTestCase()
{
    const QFileInfo source(QString::fromUtf8(__FILE__));
    QVERIFY2(source.isAbsolute(), "Compile this test using its absolute source path");
    const QString repository = QFileInfo(QDir(source.absolutePath()).filePath(QStringLiteral("../../.."))).canonicalFilePath();
    QVERIFY(!repository.isEmpty());
    const QString runtime = QDir(repository).filePath(QStringLiteral("temp/sn100_host/local_results_20261026/runtime"));
    const QString requested = QDir::cleanPath(qEnvironmentVariable("SN100_RESULT_INGEST_TEST_ROOT"));
    QVERIFY2(QDir::isAbsolutePath(requested), "Set an absolute, unused SN100_RESULT_INGEST_TEST_ROOT under repository temp/sn100_host/local_results_20261026/runtime/");
    QVERIFY2(requested.startsWith(runtime + QChar('/')) && !symlinkComponent(requested), "Runtime root must stay inside the formal test runtime fence");
    QVERIFY2(!QFileInfo::exists(requested), "Use a new runtime directory; existing evidence is never erased");
    QVERIFY(QDir().mkpath(requested));
    testRoot = QFileInfo(requested).canonicalFilePath();
    QCOMPARE(testRoot, requested);
    QVERIFY(QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE")));
    QVERIFY(!QFile::exists(QStringLiteral(":/fixtures/history_reports.json")));
    QVERIFY(!QFile::exists(QStringLiteral(":/fixtures/history_display_contract.json")));
    HistoryStore::registerTypes();
    ProjectsStore::registerTypes();
}

void ResultIngestCoreTest::emptyProfileHasNoFixture()
{
    Fixture fixture(QStringLiteral("empty_guest"), AccountSession());
    QCOMPARE(fixture.events.readyCount, 1);
    QVERIFY(fixture.events.errors.isEmpty());
    QCOMPARE(fixture.events.databasePath, fixture.historyPath);
    for (const auto &table : {"report_data", "report_results", "report_values", "report_columns"})
        QCOMPARE(fixture.count(QString::fromLatin1(table)), qint64(0));
    QCOMPARE(scalar(fixture.historyPath, QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE name='offline_metadata'")).toInt(), 0);
    fixture.query();
    QCOMPARE(fixture.events.queries.size(), 1);
    QCOMPARE(fixture.events.queries.last().total, qint64(0));
    QVERIFY(fixture.events.queries.last().reports.isEmpty());
    const QByteArray before = logicalSnapshot(fixture.historyPath);
    const qint64 request = fixture.reserve(batchFor(fixture));
    QCOMPARE(fixture.events.errors.size(), 1);
    QCOMPARE(fixture.events.errors.last().id, request);
    QCOMPARE(fixture.events.errors.last().operation, QStringLiteral("reserveLocalTasks"));
    QVERIFY(fixture.events.errors.last().message.contains(QStringLiteral("登录身份")));
    QVERIFY(fixture.events.reservations.isEmpty());
    QCOMPARE(logicalSnapshot(fixture.historyPath), before);
    const auto references = fixture.references();
    QVERIFY(references.errors.isEmpty());
    QCOMPARE(references.referenceCount, 1);
    QVERIFY(references.exists);
    QVERIFY(references.references.isEmpty());
}

void ResultIngestCoreTest::twoTasksKeepFourRawStringsAndUnknownUnits()
{
    Fixture fixture(QStringLiteral("two_tasks"), ordinaryOperator());
    QCOMPARE(fixture.events.readyCount, 1);
    const auto batch = batchFor(fixture);
    const qint64 reservation = fixture.reserve(batch);
    QVERIFY(fixture.events.errors.isEmpty());
    QCOMPARE(fixture.events.reservations.size(), 1);
    QCOMPARE(fixture.events.reservations.last().id, reservation);
    QCOMPARE(fixture.events.reservations.last().reportIds.size(), 1);
    const qint64 reportId = fixture.events.reservations.last().reportIds.first();
    QVERIFY(reportId > 0);
    QCOMPARE(scalar(fixture.historyPath, QStringLiteral("SELECT id FROM report_data WHERE row_id=101")).toLongLong(), reportId);
    for (int index = 0; index < 2; ++index)
    {
        const auto result = resultFor(batch.tasks.at(index), index);
        const qint64 request = fixture.complete(result);
        QVERIFY(fixture.events.errors.isEmpty());
        QCOMPARE(fixture.events.completions.size(), index + 1);
        QCOMPARE(fixture.events.completions.last().id, request);
        QCOMPARE(fourValues(fixture.events.completions.last().result), fourValues(result));
        const auto &persisted = fixture.events.completions.last().result;
        QCOMPARE(persisted.taskType, result.taskType);
        QCOMPARE(persisted.mdaName, result.mdaName);
        QCOMPARE(persisted.taskId, result.taskId);
        QCOMPARE(persisted.reportRowId, result.reportRowId);
        QCOMPARE(persisted.projectId, result.projectId);
        QCOMPARE(persisted.projectName, result.projectName);
        QCOMPARE(persisted.instrumentProjectId, result.instrumentProjectId);
        QCOMPARE(persisted.sampleId, result.sampleId);
        QCOMPARE(persisted.dilution, result.dilution);
        QCOMPARE(persisted.calibrationId, result.calibrationId);
        QCOMPARE(persisted.abnormalType, result.abnormalType);
        QCOMPARE(persisted.testedAt, result.testedAt);
        QCOMPARE(fixture.values(result.taskId), fourValues(result));
    }
    QCOMPARE(fixture.count(QStringLiteral("report_data")), qint64(1));
    QCOMPARE(fixture.count(QStringLiteral("report_results")), qint64(2));
    QCOMPARE(fixture.count(QStringLiteral("report_values")), qint64(8));
    QCOMPARE(fixture.count(QStringLiteral("report_columns")), qint64(4));
    QCOMPARE(scalar(fixture.historyPath, QStringLiteral("SELECT COUNT(*) FROM result_task_identity WHERE completed=1 AND uploaded=0")).toInt(), 2);
    fixture.detail(reportId);
    QCOMPARE(fixture.events.details.size(), 1);
    const auto report = fixture.events.details.last();
    QCOMPARE(report.rowId, qint64(101));
    QCOMPARE(report.results.size(), 2);
    for (int index = 0; index < 2; ++index)
    {
        const auto &result = report.results.at(index);
        QCOMPARE(result.taskId, batch.tasks.at(index).taskId);
        QCOMPARE(result.values.size(), 4);
        const auto expected = fourValues(resultFor(batch.tasks.at(index), index));
        for (int valueIndex = 0; valueIndex < 4; ++valueIndex)
        {
            const auto &value = result.values.at(valueIndex);
            QCOMPARE(value.resultType, valueIndex + 1);
            QCOMPARE(value.rawValue, expected.at(valueIndex));
            QVERIFY(value.label.contains(QStringLiteral("单位/精度未知")));
            QVERIFY(value.unit.isEmpty());
            QCOMPARE(value.decimals, -1);
            QCOMPARE(value.factor, 1.0);
            QCOMPARE(HistoryStore::displayValue(value), expected.at(valueIndex));
        }
    }
    fixture.loadDisplay();
    QVERIFY(fixture.events.errors.isEmpty());
    QCOMPARE(fixture.events.displayCount, 1);
    QCOMPARE(fixture.events.display.size(), 8); // Four patient fields + four actual result columns.
    const auto references = fixture.references();
    QVERIFY(references.errors.isEmpty());
    QCOMPARE(references.referenceCount, 1);
    QCOMPARE(references.projectId, fixture.project.id);
    QCOMPARE(references.references.size(), 2);
    for (const auto &task : batch.tasks)
        QVERIFY(references.references.contains(QStringLiteral("history_results:") + fixture.key(task.taskId)));
}

void ResultIngestCoreTest::repeatedCompletionPreservesPatientAndHiddenReport()
{
    Fixture fixture(QStringLiteral("repeat_patient_hidden"), ordinaryOperator());
    QCOMPARE(fixture.events.readyCount, 1);
    const auto batch = batchFor(fixture);
    fixture.reserve(batch);
    QVERIFY(fixture.events.errors.isEmpty());
    QCOMPARE(fixture.events.reservations.size(), 1);
    const qint64 reportId = fixture.events.reservations.last().reportIds.first();
    for (const auto &task : batch.tasks) fixture.complete(resultFor(task));
    QCOMPARE(fixture.events.completions.size(), 2);
    fixture.detail(reportId);
    QCOMPARE(fixture.events.details.size(), 1);
    auto patient = fixture.events.details.last().patient;
    patient.name = QStringLiteral("ANONEDIT1");
    patient.department = QStringLiteral("ANONKS001");
    patient.remark = QStringLiteral("anonymous edit retained");
    fixture.savePatient(patient);
    QCOMPARE(fixture.events.patients.size(), 1);
    QCOMPARE(fixture.events.patients.last().name, patient.name);
    fixture.hide(reportId);
    QCOMPARE(fixture.events.hidden.size(), 1);
    QCOMPARE(fixture.events.hidden.last(), HistoryIds{reportId});
    QVERIFY(fixture.events.errors.isEmpty());
    QCOMPARE(scalar(fixture.historyPath, QStringLiteral("SELECT visible FROM report_data WHERE id=?"), {reportId}).toInt(), 0);
    const QByteArray beforePatient = fixture.reportData();
    const auto beforeReferences = fixture.references();
    QVERIFY(beforeReferences.errors.isEmpty());
    QCOMPARE(beforeReferences.references.size(), 2);
    const QStringList otherTask = fixture.values(1002);
    fixture.session(ordinaryOperator(false)); // Existing protected task finishes while device is busy.
    const auto changed = resultFor(batch.tasks.first(), 1);
    fixture.complete(changed);
    QVERIFY(fixture.events.errors.isEmpty());
    QCOMPARE(fixture.events.completions.size(), 3);
    QCOMPARE(fixture.values(1001), fourValues(changed));
    QCOMPARE(fixture.values(1002), otherTask);
    QCOMPARE(scalar(fixture.historyPath, QStringLiteral("SELECT calibration_id FROM result_task_identity WHERE task_id=1001")).toString(), changed.calibrationId);
    QCOMPARE(scalar(fixture.historyPath, QStringLiteral("SELECT abnormal_type FROM report_results WHERE task_id=1001")).toString(), changed.abnormalType);
    QCOMPARE(scalar(fixture.historyPath, QStringLiteral("SELECT date_time FROM report_results WHERE task_id=1001")).toString(), changed.testedAt.toString(QStringLiteral("yyyyMMddHHmmss")));
    QCOMPARE(fixture.count(QStringLiteral("report_data")), qint64(1));
    QCOMPARE(fixture.count(QStringLiteral("report_results")), qint64(2));
    QCOMPARE(fixture.count(QStringLiteral("report_values")), qint64(8));
    QCOMPARE(fixture.reportData(), beforePatient);
    const auto afterReferences = fixture.references();
    QVERIFY(afterReferences.errors.isEmpty());
    QCOMPARE(afterReferences.references, beforeReferences.references);
    fixture.query();
    QCOMPARE(fixture.events.queries.size(), 1);
    QCOMPARE(fixture.events.queries.last().total, qint64(0));
}

void ResultIngestCoreTest::retestAndResetHaveDifferentIdentitySemantics()
{
    Fixture fixture(QStringLiteral("retest_reset"), ordinaryOperator());
    QCOMPARE(fixture.events.readyCount, 1);
    const auto original = batchFor(fixture);
    fixture.reserve(original);
    QCOMPARE(fixture.events.reservations.size(), 1);
    const qint64 originalReport = fixture.events.reservations.last().reportIds.first();
    for (const auto &task : original.tasks) fixture.complete(resultFor(task));
    QVERIFY(fixture.events.errors.isEmpty());
    QCOMPARE(fixture.events.completions.size(), 2);
    const auto oldFirst = fixture.values(1001), oldSecond = fixture.values(1002);
    const auto beforeHistory = logicalSnapshot(fixture.historyPath);
    const auto beforeCatalog = logicalSnapshot(fixture.catalogPath);
    const qint64 invalid = fixture.reserve(original); // Completed IDs cannot masquerade as Initial.
    QCOMPARE(fixture.events.errors.size(), 1);
    QCOMPARE(fixture.events.errors.last().id, invalid);
    QCOMPARE(fixture.events.reservations.size(), 1);
    QCOMPARE(logicalSnapshot(fixture.historyPath), beforeHistory);
    QCOMPARE(logicalSnapshot(fixture.catalogPath), beforeCatalog);
    auto retest = batchFor(fixture, 201, 2001);
    retest.mode = HistoryReservationMode::RetestNewIds;
    fixture.reserve(retest);
    QCOMPARE(fixture.events.errors.size(), 1);
    QCOMPARE(fixture.events.reservations.size(), 2);
    const qint64 retestReport = fixture.events.reservations.last().reportIds.first();
    QVERIFY(retestReport != originalReport);
    for (const auto &task : retest.tasks) fixture.complete(resultFor(task, 1));
    QCOMPARE(fixture.events.errors.size(), 1);
    QCOMPARE(fixture.count(QStringLiteral("report_data")), qint64(2));
    QCOMPARE(fixture.count(QStringLiteral("report_results")), qint64(4));
    QCOMPARE(fixture.values(1001), oldFirst);
    QCOMPARE(fixture.values(1002), oldSecond);
    const auto newFirst = fixture.values(2001), newSecond = fixture.values(2002);
    auto reset = original;
    reset.mode = HistoryReservationMode::ErrorResetSameIds;
    reset.tasks = HistoryTaskReservations{original.tasks.first()};
    reset.tasks.first().reservedAt = referenceTime().addSecs(100);
    const auto reportsBeforeReset = fixture.reportData();
    const auto referencesBeforeReset = fixture.references();
    QVERIFY(referencesBeforeReset.errors.isEmpty());
    QCOMPARE(referencesBeforeReset.references.size(), 4);
    fixture.reserve(reset); // Explicit upstream-admitted reset; store is not the execution-state scheduler.
    QCOMPARE(fixture.events.errors.size(), 1);
    QCOMPARE(fixture.events.reservations.size(), 3);
    QCOMPARE(fixture.events.reservations.last().reportIds, HistoryIds{originalReport});
    QCOMPARE(fixture.count(QStringLiteral("report_data")), qint64(2));
    QCOMPARE(fixture.count(QStringLiteral("report_results")), qint64(4));
    QVERIFY(fixture.values(1001).isEmpty());
    QCOMPARE(fixture.values(1002), oldSecond);
    QCOMPARE(fixture.values(2001), newFirst);
    QCOMPARE(fixture.values(2002), newSecond);
    QCOMPARE(scalar(fixture.historyPath, QStringLiteral("SELECT completed FROM result_task_identity WHERE task_id=1001")).toInt(), 0);
    QCOMPARE(scalar(fixture.historyPath, QStringLiteral("SELECT uploaded FROM result_task_identity WHERE task_id=1001")).toInt(), 0);
    QVERIFY(scalar(fixture.historyPath, QStringLiteral("SELECT calibration_id FROM result_task_identity WHERE task_id=1001")).toString().isEmpty());
    QVERIFY(scalar(fixture.historyPath, QStringLiteral("SELECT abnormal_type FROM report_results WHERE task_id=1001")).toString().isEmpty());
    QCOMPARE(fixture.reportData(), reportsBeforeReset);
    const auto referencesAfterReset = fixture.references();
    QVERIFY(referencesAfterReset.errors.isEmpty());
    QCOMPARE(referencesAfterReset.references, referencesBeforeReset.references);
}

void ResultIngestCoreTest::realAbortRollsBackDataAndOnlyNewHolds()
{
    Fixture fixture(QStringLiteral("real_sqlite_abort"), ordinaryOperator());
    QCOMPARE(fixture.events.readyCount, 1);
    const auto original = batchFor(fixture);
    fixture.reserve(original);
    QCOMPARE(fixture.events.reservations.size(), 1);
    for (const auto &task : original.tasks) fixture.complete(resultFor(task));
    QVERIFY(fixture.events.errors.isEmpty());
    QCOMPARE(fixture.events.completions.size(), 2);
    executeOwnedDdl(fixture.historyPath, QStringLiteral(
        "CREATE TRIGGER anonymous_completion_abort BEFORE UPDATE OF raw_value ON report_values "
        "WHEN NEW.task_id=1001 AND NEW.result_type=3 BEGIN SELECT RAISE(ABORT,'ANON_REAL_ABORT'); END"));
    const auto beforeHistory = logicalSnapshot(fixture.historyPath);
    const auto beforeCatalog = logicalSnapshot(fixture.catalogPath);
    const qint64 completion = fixture.complete(resultFor(original.tasks.first(), 1));
    QCOMPARE(fixture.events.errors.size(), 1);
    QCOMPARE(fixture.events.errors.last().id, completion);
    QVERIFY(fixture.events.errors.last().message.contains(QStringLiteral("ANON_REAL_ABORT")));
    QCOMPARE(fixture.events.completions.size(), 2);
    QCOMPARE(logicalSnapshot(fixture.historyPath), beforeHistory);
    QCOMPARE(logicalSnapshot(fixture.catalogPath), beforeCatalog);
    executeOwnedDdl(fixture.historyPath, QStringLiteral("DROP TRIGGER anonymous_completion_abort"));

    executeOwnedDdl(fixture.historyPath, QStringLiteral(
        "CREATE TRIGGER anonymous_reservation_abort BEFORE INSERT ON report_results "
        "WHEN NEW.task_id=3001 BEGIN SELECT RAISE(ABORT,'ANON_RESERVE_ABORT'); END"));
    const auto beforeReservationHistory = logicalSnapshot(fixture.historyPath);
    const auto beforeReservationCatalog = logicalSnapshot(fixture.catalogPath);
    const auto protectedBefore = fixture.references();
    QVERIFY(protectedBefore.errors.isEmpty());
    QCOMPARE(protectedBefore.references.size(), 2);
    auto fresh = batchFor(fixture, 301, 3001);
    fresh.tasks = HistoryTaskReservations{fresh.tasks.first()};
    const qint64 reservation = fixture.reserve(fresh);
    QCOMPARE(fixture.events.errors.size(), 2);
    QCOMPARE(fixture.events.errors.last().id, reservation);
    QVERIFY(fixture.events.errors.last().message.contains(QStringLiteral("ANON_RESERVE_ABORT")));
    QCOMPARE(fixture.events.reservations.size(), 1);
    QCOMPARE(logicalSnapshot(fixture.historyPath), beforeReservationHistory);
    QCOMPARE(logicalSnapshot(fixture.catalogPath), beforeReservationCatalog);
    const auto protectedAfter = fixture.references();
    QVERIFY(protectedAfter.errors.isEmpty());
    QCOMPARE(protectedAfter.references, protectedBefore.references);
    QVERIFY(!protectedAfter.references.contains(QStringLiteral("history_results:") + fixture.key(3001)));
    // These are real SQLite ABORT failures. No simulated mid-fetch/old-incident claim.
}

void ResultIngestCoreTest::reopenChecksActualReferencesAndRejectsWrongIdentity()
{
    Fixture fixture(QStringLiteral("reopen_identity"), ordinaryOperator());
    QCOMPARE(fixture.events.readyCount, 1);
    const auto batch = batchFor(fixture);
    fixture.reserve(batch);
    QCOMPARE(fixture.events.reservations.size(), 1);
    const qint64 reportId = fixture.events.reservations.last().reportIds.first();
    for (const auto &task : batch.tasks) fixture.complete(resultFor(task));
    QVERIFY(fixture.events.errors.isEmpty());
    const QString storeId = fixture.storeId();
    const auto beforeHistory = logicalSnapshot(fixture.historyPath);
    const auto beforeCatalog = logicalSnapshot(fixture.catalogPath);
    const auto beforeReferences = fixture.references();
    QVERIFY(beforeReferences.errors.isEmpty());
    QCOMPARE(beforeReferences.references.size(), 2);
    fixture.close();
    QCOMPARE(fixture.events.closedCount, 1);
    fixture.session(AccountSession()); // Reopen/read-only validation must not need engineer or new holds.
    fixture.open();
    QCOMPARE(fixture.events.readyCount, 2);
    QVERIFY(fixture.events.errors.isEmpty());
    QCOMPARE(fixture.storeId(), storeId);
    QCOMPARE(logicalSnapshot(fixture.historyPath), beforeHistory);
    QCOMPARE(logicalSnapshot(fixture.catalogPath), beforeCatalog);
    fixture.detail(reportId);
    QCOMPARE(fixture.events.details.size(), 1);
    QCOMPARE(fixture.events.details.last().results.size(), 2);
    QCOMPARE(fixture.values(1001), fourValues(resultFor(batch.tasks.first())));
    for (int kind = 0; kind < 3; ++kind)
    {
        auto wrong = resultFor(batch.tasks.first(), 1);
        if (kind == 0) wrong.projectName = QStringLiteral("OTHER_NAME");
        if (kind == 1) wrong.reportRowId = 999;
        if (kind == 2) wrong.taskId = 9999;
        const qint64 request = fixture.complete(wrong);
        QCOMPARE(fixture.events.errors.size(), kind + 1);
        QCOMPARE(fixture.events.errors.last().id, request);
        QCOMPARE(fixture.events.completions.size(), 2);
        QCOMPARE(logicalSnapshot(fixture.historyPath), beforeHistory);
        QCOMPARE(logicalSnapshot(fixture.catalogPath), beforeCatalog);
    }
    const auto afterWrongIdentity = fixture.references();
    QVERIFY(afterWrongIdentity.errors.isEmpty());
    QCOMPARE(afterWrongIdentity.references, beforeReferences.references);

    fixture.close();
    // Explicit separate ordinary-operator test actor creates a genuine missing
    // reference state using the actual release API. Business session stays Guest.
    const auto release = fixture.releaseAsExplicitOperator(fixture.key(1001));
    QCOMPARE(release.readyCount, 1);
    QVERIFY(release.errors.isEmpty());
    QCOMPARE(release.successCount, 1);
    const auto remaining = fixture.references();
    QVERIFY(remaining.errors.isEmpty());
    QCOMPARE(remaining.references.size(), 1);
    QVERIFY(remaining.references.contains(QStringLiteral("history_results:") + fixture.key(1002)));
    const auto missingCatalog = logicalSnapshot(fixture.catalogPath);
    fixture.open();
    QCOMPARE(fixture.events.readyCount, 2); // No false ready/recovery acknowledgment.
    QVERIFY(!fixture.events.open);
    QCOMPARE(fixture.events.errors.size(), 4);
    QCOMPARE(fixture.events.errors.last().operation, QStringLiteral("initializeLocalResults"));
    QVERIFY(fixture.events.errors.last().message.contains(QStringLiteral("引用缺失")));
    QCOMPARE(logicalSnapshot(fixture.historyPath), beforeHistory);
    QCOMPARE(logicalSnapshot(fixture.catalogPath), missingCatalog);
    const auto afterFailedReopen = fixture.references();
    QVERIFY(afterFailedReopen.errors.isEmpty());
    QCOMPARE(afterFailedReopen.references, remaining.references);
}

void ResultIngestCoreTest::localResultCsvExportIsBlockedUntilUnitsAreMapped()
{
    Fixture fixture(QStringLiteral("local_result_csv_blocked"), ordinaryOperator());
    QCOMPARE(fixture.events.readyCount, 1);
    QVERIFY(fixture.events.errors.isEmpty());
    auto batch = batchFor(fixture);
    batch.tasks = HistoryTaskReservations{batch.tasks.first()};
    fixture.reserve(batch);
    QCOMPARE(fixture.events.reservations.size(), 1);
    fixture.complete(resultFor(batch.tasks.first()));
    QCOMPARE(fixture.events.completions.size(), 1);
    QVERIFY(fixture.events.errors.isEmpty());

    const QDir exports(QDir(fixture.historyDirectory).filePath(QStringLiteral("exports")));
    QVERIFY(exports.exists());
    const QString path = exports.filePath(QStringLiteral("unit_mapping_blocked.csv"));
    QVERIFY(QDir::isAbsolutePath(path));
    QVERIFY(!QFileInfo::exists(path));
    const QDir::Filters entries = QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot;
    QVERIFY(exports.entryList(entries).isEmpty());
    const qint64 request = 7001;
    const int exportedSignals = fixture.exportCsv(request, path);
    QCOMPARE(exportedSignals, 0);
    QCOMPARE(fixture.events.errors.size(), 1);
    QCOMPARE(fixture.events.errors.last().id, request);
    QCOMPARE(fixture.events.errors.last().operation, QStringLiteral("exportCsv"));
    QCOMPARE(fixture.events.errors.last().message,
             QStringLiteral("本地结果的单位和精度尚未映射，暂不开放文件输出。"));
    QVERIFY(!QFileInfo::exists(path));
    QVERIFY(exports.entryList(entries).isEmpty());
}

QTEST_GUILESS_MAIN(ResultIngestCoreTest)
#include "result_ingest_store_test.moc"
