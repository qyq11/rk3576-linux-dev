#include "alarm_history_store.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <functional>

using namespace AlarmHistory;

namespace
{
// Formal tests keep runtime data under this repository's temp/sn100_host.
// The caller must create a fresh empty dedicated directory before invocation.
// Resolve every existing ancestor, rejecting links/junction aliases and escapes.
QString anonymousTestRootError(const QString &suppliedRoot, QString *canonicalRoot)
{
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity pathCase = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity pathCase = Qt::CaseSensitive;
#endif
    const QString sourceFile = QFileInfo(QString::fromUtf8(__FILE__)).canonicalFilePath();
    if (sourceFile.isEmpty())
    {
        return QStringLiteral("无法从正式测试源码定位项目根；未创建匿名库。");
    }
    QDir repository(QFileInfo(sourceFile).absolutePath());
    if (repository.dirName() != QStringLiteral("tests") || !repository.cdUp() ||
        repository.dirName() != QStringLiteral("sn100_host") || !repository.cdUp() ||
        repository.dirName() != QStringLiteral("06_programs") || !repository.cdUp())
    {
        return QStringLiteral("测试源码不在本项目06_programs/sn100_host/tests，拒绝猜测输出位置。");
    }
    const QString repositoryRoot = repository.canonicalPath();
    const QString base = QDir::cleanPath(QDir(repositoryRoot).filePath(QStringLiteral("temp/sn100_host")));
    if (repositoryRoot.isEmpty() || suppliedRoot.isEmpty() || suppliedRoot.contains(QChar::Null) ||
        !QFileInfo(suppliedRoot).isAbsolute())
    {
        return QStringLiteral("须显式指定本项目内的匿名测试目录绝对路径。");
    }
    const QString root = QDir::cleanPath(QDir::fromNativeSeparators(suppliedRoot));
    const QString resolvedRoot = QFileInfo(root).canonicalFilePath();
    if (!root.startsWith(base + QLatin1Char('/'), pathCase) || resolvedRoot.isEmpty() ||
        !resolvedRoot.startsWith(base + QLatin1Char('/'), pathCase))
    {
        return QStringLiteral("匿名目录必须是本项目temp/sn100_host内已存在的专用子目录。");
    }
    QString ancestor = root;
    while (ancestor.compare(repositoryRoot, pathCase) != 0)
    {
        const QFileInfo info(ancestor);
        const QString resolvedAncestor = info.canonicalFilePath();
        if (!info.exists() || !info.isDir() || info.isSymLink() || resolvedAncestor.isEmpty() ||
            QDir::cleanPath(resolvedAncestor).compare(ancestor, pathCase) != 0)
        {
            return QStringLiteral("匿名目录或现有祖先含链接、路径别名或不是目录，拒绝路径逃逸。");
        }
        const QString parent = QDir::cleanPath(info.absolutePath());
        if (parent == ancestor)
        {
            return QStringLiteral("匿名目录祖先未回到本项目根，拒绝继续。");
        }
        ancestor = parent;
    }
    const QDir directory(resolvedRoot);
    const QFileInfo directoryInfo(resolvedRoot);
    if (!directoryInfo.isReadable() || !directoryInfo.isWritable())
    {
        return QStringLiteral("匿名专用目录须可读取和写入，无法检查已有内容时拒绝继续。");
    }
    if (!directory.entryList(QDir::AllEntries | QDir::Hidden | QDir::System |
                             QDir::NoDotAndDotDot).isEmpty())
    {
        return QStringLiteral("匿名专用目录必须为空；发现已有文件、子目录或链接，未覆盖或清理。");
    }
    *canonicalRoot = resolvedRoot;
    return QString();
}

bool waitUntil(const std::function<bool()> &predicate)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!predicate() && elapsed.elapsed() < 5000)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QTest::qWait(1);
    }
    return predicate();
}
QByteArray fileSha(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
    {
        return {};
    }
    return hash.result();
}

// Writable SQL is restricted to this future-test-only anonymous fixture helper.
// The Store itself has no create/insert/update/delete API or fallback fixture.
bool writeFixture(const QString &path, const QString &mutation = QString(), bool wrongDateDeclaration = false)
{
    const QString connection = QStringLiteral("anonymous_alarm_fixture_") + QUuid::createUuid().toString();
    bool good = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(path);
        if (db.open())
        {
            QSqlQuery query(db);
            QString schema = QStringLiteral("CREATE TABLE abnormal_log("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,error_code VARCHAR,error_describe VARCHAR,"
                "measure VARCHAR,date_time DATETIME,corrective_action VARCHAR,use_user VARCHAR)");
            if (wrongDateDeclaration) schema.replace(QStringLiteral("date_time DATETIME"), QStringLiteral("date_time TEXT"));
            good = query.exec(schema);
            for (int id = 1; good && id <= 26; ++id)
            {
                QString stamp;
                if (id == 1) stamp = QStringLiteral("2026/10/26 00:00:00");
                else if (id == 2) stamp = QStringLiteral("2026/10/26 23:59:59");
                else if (id == 3 || id == 4) stamp = QStringLiteral("2026/10/26 12:00:00");
                else if (id <= 23) stamp = QStringLiteral("2026/10/26 10:00:%1").arg(id, 2, 10, QLatin1Char('0'));
                else if (id == 24) stamp = QStringLiteral("2026/10/25 00:00:00");
                else if (id == 25) stamp = QStringLiteral("2026/10/25 23:59:59");
                else stamp = QStringLiteral("2026/10/27 00:00:00");
                query.prepare(QStringLiteral("INSERT INTO abnormal_log VALUES(?,?,?,?,?,?,?)"));
                query.addBindValue(id);
                query.addBindValue(id == 3 || id == 4 ? QStringLiteral("E*重复") : QStringLiteral("E%1").arg(id));
                query.addBindValue(QStringLiteral("匿名描述%1").arg(id));
                query.addBindValue(QStringLiteral("原存措施%1").arg(id));
                query.addBindValue(stamp);
                query.addBindValue(id == 3 ? QVariant(QStringLiteral("")) : QVariant(QVariant::String));
                query.addBindValue(QStringLiteral("匿名操作者%1").arg(id));
                good = query.exec();
            }
            if (good && !mutation.isEmpty())
            {
                good = query.exec(mutation);
            }
            query.finish();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return good;
}

class ExclusiveLock
{
public:
    explicit ExclusiveLock(const QString &path)
        : name(QStringLiteral("anonymous_alarm_lock_") + QUuid::createUuid().toString())
    {
        db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        if (db.open())
        {
            QSqlQuery query(db);
            locked = query.exec(QStringLiteral("BEGIN EXCLUSIVE"));
        }
    }
    ~ExclusiveLock()
    {
        if (locked) db.rollback();
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
    }
    bool locked = false;
private:
    QString name;
    QSqlDatabase db;
};

class Worker : public QObject
{
public:
    explicit Worker(const QString &path)
    {
        store = new Store;
        store->moveToThread(&thread);
        connect(&thread, &QThread::finished, store, &QObject::deleteLater);
        connect(store, &Store::closed, &thread, &QThread::quit, Qt::DirectConnection);
        connect(store, &Store::closed, this, [this]() { closed = true; });
        connect(store, &Store::opened, this, [this](const QString &value, const QString &)
        { token = value; opened = true; });
        connect(store, &Store::openFailed, this, [this](Error value, const QString &message)
        { error = value; failureMessage = message; openFailure = true; });
        connect(store, &Store::continuationNeeded, this, [this](Request request, qint64, quint64 step)
        {
            ++continuations;
            lastReply = request;
            nextStep = step;
            if (autoAdvance) advance(request.key, step);
        });
        connect(store, &Store::batchReady, this, [this](Request request, Rows batch, bool more, quint64 step)
        {
            lastReply = request;
            nextStep = step;
            maxBatch = qMax(maxBatch, batch.size());
            rows.append(batch); // Test-only aggregation of exactly 26 anonymous rows.
            ++batches;
            if (more && !pauseAfterBatch && autoAdvance) advance(request.key, step);
        });
        connect(store, &Store::finished, this, [this](Request request, Completion value, qint64 delivered, qint64 matching)
        { lastReply = request; completion = value; returned = delivered; totalMatching = matching; done = true; ++terminals; });
        connect(store, &Store::failed, this, [this](Request request, Error value, const QString &message, qint64 delivered)
        { lastReply = request; error = value; failureMessage = message; returned = delivered; failed = done = true; ++terminals; });
        connect(store, &Store::cancelled, this, [this](Request request, qint64 delivered)
        { lastReply = request; returned = delivered; wasCancelled = done = true; ++terminals; });
        connect(store, &Store::requestRejected, this, [this](Key key, Error value, const QString &)
        { rejectedKey = key; rejectedError = value; ++rejections; });
        thread.start();
        QTimer::singleShot(0, store, [this, path]() { store->openExisting(path); });
    }
    ~Worker() override { shutdown(); }
    bool ready() { return waitUntil([this]() { return opened || openFailure; }); }
    Request request(Scope scope = Scope::DateInterval)
    {
        Request result;
        result.key = {++serial, 7, token};
        result.scope = scope;
        result.first = result.last = result.today = QDate(2026, 10, 26);
        result.batchSize = 3;
        return result;
    }
    void begin(Request request) { if (!closing) QTimer::singleShot(0, store, [this, request]() { store->begin(request); }); }
    void advance(Key key, quint64 step = 0)
    {
        if (step == 0) step = nextStep;
        if (!closing) QTimer::singleShot(0, store, [this, key, step]() { store->advance(key, step); });
    }
    void cancel(Key key) { if (!closing) QTimer::singleShot(0, store, [this, key]() { store->cancel(key); }); }
    void shutdown()
    {
        closing = true; // Late queued progress must not schedule against a deleted worker.
        if (thread.isRunning())
        {
            QTimer::singleShot(0, store, [this]() { store->close(); });
            if (!thread.wait(5000)) qFatal("Alarm history worker did not close normally; do not terminate it");
            QCoreApplication::processEvents();
        }
    }
    Store *store = nullptr;
    QThread thread;
    QString token;
    QString failureMessage;
    Request lastReply;
    Key rejectedKey;
    Rows rows;
    Error error = Error::SqlFailure;
    Error rejectedError = Error::SqlFailure;
    Completion completion = Completion::IntervalComplete;
    bool opened = false, openFailure = false, closed = false, closing = false;
    bool done = false, failed = false, wasCancelled = false;
    bool autoAdvance = true, pauseAfterBatch = false;
    quint64 serial = 0;
    quint64 nextStep = 0;
    int terminals = 0, rejections = 0, continuations = 0, batches = 0, maxBatch = 0;
    qint64 returned = 0, totalMatching = 0;
};
QList<qint64> ids(const Rows &rows)
{
    QList<qint64> result;
    for (const Row &row : rows) result.append(row.id);
    return result;
}
}

class AlarmHistoryStoreTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        Store::registerTypes();
        QVERIFY2(QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE")), "Existing QSQLITE required; do not install in this test");
        QString root;
        const QString rootError = anonymousTestRootError(qEnvironmentVariable("SN100_ALARM_TEST_ROOT"), &root);
        QVERIFY2(rootError.isEmpty(), qPrintable(rootError));
        temporary.reset(new QTemporaryDir(QDir(root).filePath(QStringLiteral("anonymous_alarm_XXXXXX"))));
        QVERIFY(temporary->isValid());
    }
    void latestTwentyAndSevenFields()
    {
        const QString path = freshPath();
        QVERIFY(writeFixture(path));
        const QByteArray before = fileSha(path);
        Worker worker(path);
        QVERIFY(worker.ready() && worker.opened);
        Request request = worker.request(Scope::LatestToday20);
        worker.begin(request);
        QVERIFY(waitUntil([&worker]() { return worker.done; }));
        QVERIFY(!worker.failed && !worker.wasCancelled);
        QCOMPARE(worker.returned, qint64(20));
        QCOMPARE(worker.totalMatching, qint64(23));
        QVERIFY(worker.completion == Completion::RecentTodayComplete);
        QList<qint64> expected = {2, 4, 3};
        for (qint64 id = 23; id >= 7; --id) expected.append(id);
        QCOMPARE(ids(worker.rows), expected);
        QCOMPARE(worker.rows.at(1).errorCode, QStringLiteral("E*重复"));
        QCOMPARE(worker.rows.at(1).description, QStringLiteral("匿名描述4"));
        QCOMPARE(worker.rows.at(1).measure, QStringLiteral("原存措施4"));
        QCOMPARE(worker.rows.at(1).dateTime, QStringLiteral("2026/10/26 12:00:00"));
        QCOMPARE(worker.rows.at(1).user, QStringLiteral("匿名操作者4"));
        QVERIFY(worker.rows.at(1).correctiveAction.isNull());
        QVERIFY(!worker.rows.at(2).correctiveAction.isNull() && worker.rows.at(2).correctiveAction.isEmpty());
        QVERIFY(worker.lastReply.key == request.key);
        QCOMPARE(worker.lastReply.first, request.first);
        QVERIFY(worker.maxBatch <= request.batchSize);
        worker.shutdown();
        QVERIFY(worker.closed && !worker.thread.isRunning());
        QCOMPARE(fileSha(path), before);
    }
    void fullInclusiveRangeBatches()
    {
        const QString path = freshPath();
        QVERIFY(writeFixture(path));
        const QByteArray before = fileSha(path);
        Worker worker(path);
        QVERIFY(worker.ready() && worker.opened);
        Request request = worker.request();
        request.first = QDate(2026, 10, 25);
        worker.begin(request);
        QVERIFY(waitUntil([&worker]() { return worker.done; }));
        QVERIFY(!worker.failed && worker.completion == Completion::IntervalComplete);
        QList<qint64> expected = {2, 4, 3};
        for (qint64 id = 23; id >= 5; --id) expected.append(id);
        expected << 1 << 25 << 24;
        QCOMPARE(ids(worker.rows), expected);
        QCOMPARE(worker.returned, qint64(25));
        QCOMPARE(worker.totalMatching, qint64(25));
        QVERIFY(worker.batches > 1 && worker.maxBatch <= 3);
        worker.shutdown();
        QCOMPARE(fileSha(path), before);
    }
    void legitimateEmptyRange()
    {
        const QString path = freshPath();
        QVERIFY(writeFixture(path));
        Worker worker(path);
        QVERIFY(worker.ready() && worker.opened);
        Request request = worker.request();
        request.first = request.last = QDate(2026, 10, 24);
        worker.begin(request);
        QVERIFY(waitUntil([&worker]() { return worker.done; }));
        QVERIFY(!worker.failed && !worker.wasCancelled && worker.rows.isEmpty());
        QCOMPARE(worker.returned, qint64(0));
        QCOMPARE(worker.totalMatching, qint64(0));
        QVERIFY(worker.completion == Completion::IntervalComplete);
    }
    void invalidRequests_data()
    {
        QTest::addColumn<int>("variant");
        for (int variant = 0; variant < 7; ++variant)
            QTest::newRow(qPrintable(QString::number(variant))) << variant;
    }
    void invalidRequests()
    {
        QFETCH(int, variant);
        const QString path = freshPath();
        QVERIFY(writeFixture(path));
        const QByteArray before = fileSha(path);
        Worker worker(path);
        QVERIFY(worker.ready() && worker.opened);
        Request request = worker.request();
        if (variant == 0) request.first = QDate();
        if (variant == 1) request.first = request.last.addDays(1);
        if (variant == 2) request.key.sourceToken = QStringLiteral("old-source");
        if (variant == 3) request.key.viewGeneration = 0;
        if (variant == 4) request.batchSize = 129;
        if (variant == 5) request.scope = static_cast<Scope>(99);
        if (variant == 6) { request.scope = Scope::LatestToday20; request.today = request.first.addDays(1); }
        worker.begin(request);
        QVERIFY(waitUntil([&worker]() { return worker.rejections == 1; }));
        QVERIFY(worker.rejectedError == Error::InvalidRequest && worker.terminals == 0 && worker.rows.isEmpty());
        worker.shutdown();
        QCOMPARE(fileSha(path), before);
    }
    void cancelBeforeRowsAndRejectStaleAdvance()
    {
        const QString path = freshPath();
        QVERIFY(writeFixture(path));
        Worker worker(path);
        QVERIFY(worker.ready() && worker.opened);
        worker.autoAdvance = false;
        const Request request = worker.request();
        worker.begin(request);
        QVERIFY(waitUntil([&worker]() { return worker.continuations == 1; }));
        worker.cancel(request.key);
        QVERIFY(waitUntil([&worker]() { return worker.done; }));
        QVERIFY(worker.wasCancelled && worker.rows.isEmpty() && worker.terminals == 1);
        worker.advance(request.key);
        QVERIFY(waitUntil([&worker]() { return worker.rejections == 1; }));
        QCOMPARE(worker.terminals, 1);
    }
    void staleCancelDoesNotCancelCurrentThenCancelPartial()
    {
        const QString path = freshPath();
        QVERIFY(writeFixture(path));
        const QByteArray before = fileSha(path);
        Worker worker(path);
        QVERIFY(worker.ready() && worker.opened);
        worker.pauseAfterBatch = true;
        Request request = worker.request();
        request.batchSize = 2;
        worker.begin(request);
        QVERIFY(waitUntil([&worker]() { return worker.batches == 1; }));
        Key stale = request.key;
        ++stale.viewGeneration;
        worker.cancel(stale);
        QVERIFY(waitUntil([&worker]() { return worker.rejections == 1; }));
        QVERIFY(!worker.done && worker.rows.size() == 2);
        worker.cancel(request.key);
        QVERIFY(waitUntil([&worker]() { return worker.done; }));
        QVERIFY(worker.wasCancelled && worker.terminals == 1);
        QCOMPARE(worker.returned, qint64(2));
        worker.shutdown();
        QCOMPARE(fileSha(path), before);
    }
    void duplicateContinuationCreditRejected()
    {
        const QString path = freshPath();
        QVERIFY(writeFixture(path));
        Worker worker(path);
        QVERIFY(worker.ready() && worker.opened);
        worker.autoAdvance = false;
        const Request request = worker.request();
        worker.begin(request);
        QVERIFY(waitUntil([&worker]() { return worker.continuations == 1; }));
        const quint64 firstCredit = worker.nextStep;
        worker.advance(request.key, firstCredit);
        worker.advance(request.key, firstCredit);
        QVERIFY(waitUntil([&worker]() { return worker.rejections == 1; }));
        QVERIFY(worker.rejectedError == Error::InvalidRequest && worker.terminals == 0);
        worker.cancel(request.key);
        QVERIFY(waitUntil([&worker]() { return worker.done; }));
        QVERIFY(worker.wasCancelled && worker.terminals == 1);
    }
    void busyAndCloseActiveRequest()
    {
        const QString path = freshPath();
        QVERIFY(writeFixture(path));
        Worker worker(path);
        QVERIFY(worker.ready() && worker.opened);
        worker.autoAdvance = false;
        const Request first = worker.request();
        worker.begin(first);
        QVERIFY(waitUntil([&worker]() { return worker.continuations == 1; }));
        worker.begin(worker.request());
        QVERIFY(waitUntil([&worker]() { return worker.rejections == 1; }));
        QVERIFY(worker.rejectedError == Error::Busy && worker.terminals == 0);
        worker.shutdown();
        QVERIFY(worker.closed && worker.wasCancelled && worker.terminals == 1);
    }
    void missingDoesNotCreateHistory()
    {
        const QString path = freshPath();
        Worker worker(path);
        QVERIFY(worker.ready() && worker.openFailure && !worker.opened);
        QVERIFY(worker.error == Error::OpenFailed && !QFile::exists(path));
    }
    void reusedRequestIdentityRejected()
    {
        const QString path = freshPath();
        QVERIFY(writeFixture(path));
        Worker worker(path);
        QVERIFY(worker.ready() && worker.opened);
        Request request = worker.request();
        request.first = request.last = QDate(2026, 10, 24);
        worker.begin(request);
        QVERIFY(waitUntil([&worker]() { return worker.done; }));
        QVERIFY(!worker.failed && worker.terminals == 1);
        worker.begin(request);
        QVERIFY(waitUntil([&worker]() { return worker.rejections == 1; }));
        QVERIFY(worker.rejectedError == Error::InvalidRequest && worker.terminals == 1);
    }
    void incompatibleSchema_data()
    {
        QTest::addColumn<QString>("mutation");
        QTest::addColumn<bool>("wrongDateDeclaration");
        QTest::newRow("missing") << QStringLiteral("DROP TABLE abnormal_log") << false;
        QTest::newRow("extra-column") << QStringLiteral("ALTER TABLE abnormal_log ADD COLUMN guessed_severity INTEGER") << false;
        QTest::newRow("wrong-declaration") << QString() << true;
    }
    void incompatibleSchema()
    {
        QFETCH(QString, mutation);
        QFETCH(bool, wrongDateDeclaration);
        const QString path = freshPath();
        QVERIFY(writeFixture(path, mutation, wrongDateDeclaration));
        Worker worker(path);
        QVERIFY(worker.ready() && worker.openFailure && !worker.opened);
        QVERIFY(worker.error == Error::IncompatibleSchema);
    }
    void incompatibleData_data()
    {
        QTest::addColumn<QString>("mutation");
        QTest::addColumn<int>("expectedError");
        QTest::newRow("ISO-outside-range") << QStringLiteral("UPDATE abnormal_log SET date_time='2026-10-27 00:00:00' WHERE id=26") << int(Error::IncompatibleData);
        QTest::newRow("invalid-calendar") << QStringLiteral("UPDATE abnormal_log SET date_time='2026/02/30 12:00:00' WHERE id=26") << int(Error::IncompatibleData);
        QTest::newRow("null-date") << QStringLiteral("UPDATE abnormal_log SET date_time=NULL WHERE id=26") << int(Error::IncompatibleData);
        QTest::newRow("blob-description") << QStringLiteral("UPDATE abnormal_log SET error_describe=X'4142' WHERE id=26") << int(Error::IncompatibleData);
        QTest::newRow("oversize") << QStringLiteral("UPDATE abnormal_log SET error_describe=replace(hex(zeroblob(32769)),'0','A') WHERE id=26") << int(Error::ResourceLimit);
    }
    void incompatibleData()
    {
        QFETCH(QString, mutation);
        QFETCH(int, expectedError);
        const QString path = freshPath();
        QVERIFY(writeFixture(path, mutation));
        const QByteArray before = fileSha(path);
        Worker worker(path);
        QVERIFY(worker.ready() && worker.opened);
        worker.begin(worker.request());
        QVERIFY(waitUntil([&worker]() { return worker.done; }));
        QVERIFY(worker.failed && !worker.wasCancelled && worker.rows.isEmpty());
        QCOMPARE(int(worker.error), expectedError);
        QCOMPARE(worker.terminals, 1);
        QVERIFY(!worker.failureMessage.isEmpty());
        worker.shutdown();
        QCOMPARE(fileSha(path), before);
    }
    void corruptIsErrorNotEmpty()
    {
        const QString path = freshPath();
        { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("anonymous-not-sqlite"), qint64(20)); }
        Worker worker(path);
        QVERIFY(worker.ready() && worker.openFailure && !worker.opened);
        QVERIFY(!worker.failureMessage.isEmpty());
    }
    void lockedIsErrorNotEmpty()
    {
        const QString path = freshPath();
        QVERIFY(writeFixture(path));
        const QByteArray before = fileSha(path);
        {
            ExclusiveLock lock(path);
            QVERIFY(lock.locked);
            Worker worker(path);
            QVERIFY(worker.ready() && worker.openFailure && !worker.opened);
            QVERIFY(worker.error == Error::OpenFailed || worker.error == Error::SqlFailure);
            QVERIFY(!worker.failureMessage.isEmpty() && worker.rows.isEmpty());
        }
        QCOMPARE(fileSha(path), before);
    }
    void cleanupTestCase()
    {
        for (const QString &name : QSqlDatabase::connectionNames())
            QVERIFY2(!name.startsWith(QStringLiteral("alarm_history_")), qPrintable(name));
    }
private:
    QString freshPath()
    {
        return QDir(temporary->path()).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".sqlite"));
    }
    std::unique_ptr<QTemporaryDir> temporary;
};
QTEST_GUILESS_MAIN(AlarmHistoryStoreTest)
#include "alarm_history_store_test.moc"
