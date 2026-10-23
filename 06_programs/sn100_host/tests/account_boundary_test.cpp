// Independent storage/path boundaries for O02-1. Every fixture is newly created
// under SN100_BOUNDARY_DIR; no reference database or credentials are opened.
// Transient secrets are UUIDs. Evidence contains public fields only, never the
// authentication DB, its digest, password text, or a password-derived digest.
#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>

#include "data/account_store.h"

namespace
{
const char kDatabaseName[] = "user_Information.db";
const QByteArray kTag("SN100_OFFLINE_O01_V1\n");

QString secret()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QByteArray bytes(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::NewOnly)
            && file.write(data) == data.size();
}

QString databasePath(const QString &directory)
{
    return QDir(directory).filePath(QString::fromLatin1(kDatabaseName));
}

bool createSqlite(const QString &path, const QStringList &statements)
{
    const QString connection = QStringLiteral("boundary-fixture-") + secret();
    bool result = true;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setDatabaseName(path);
        result = database.open();
        if (result)
        {
            QSqlQuery query(database);
            for (const QString &statement : statements)
            {
                if (!query.exec(statement))
                {
                    result = false;
                    break;
                }
                query.finish();
            }
        }
        database.close();
    }
    QSqlDatabase::removeDatabase(connection);
    return result;
}

// Actual database work remains in the real AccountStore's worker. These
// boundary tests have no GUI latency claim, so a blocking queued invocation is
// intentional: completion is deterministic, and it never changes ownership.
class Worker
{
public:
    Worker() : store(new AccountStore), ready(store, &AccountStore::ready),
        errors(store, &AccountStore::error), successes(store, &AccountStore::succeeded),
        sessions(store, &AccountStore::sessionChanged), rows(store, &AccountStore::loaded)
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
    void clear()
    {
        ready.clear(); errors.clear(); successes.clear(); sessions.clear(); rows.clear();
    }
    bool initialize(const QString &directory, const QString &initialSecret = QString())
    {
        clear();
        const bool invoked = QMetaObject::invokeMethod(store, "initialize", Qt::BlockingQueuedConnection,
            Q_ARG(QString, directory), Q_ARG(QString, initialSecret));
        QCoreApplication::processEvents();
        return invoked && ready.size() == 1 && errors.isEmpty();
    }
    bool login(const QString &name, bool actionReady = true)
    {
        clear();
        const bool invoked = QMetaObject::invokeMethod(store, "setSession", Qt::BlockingQueuedConnection,
            Q_ARG(QString, name), Q_ARG(bool, actionReady));
        QCoreApplication::processEvents();
        return invoked && !sessions.isEmpty() && errors.isEmpty();
    }
    AccountSession session() const
    {
        return sessions.isEmpty() ? AccountSession()
            : qvariant_cast<AccountSession>(sessions.last().at(0));
    }
    bool add(const QString &name, const QString &password, int role)
    {
        clear();
        const bool invoked = QMetaObject::invokeMethod(store, "add", Qt::BlockingQueuedConnection,
            Q_ARG(QString, name), Q_ARG(QString, password), Q_ARG(QString, password), Q_ARG(int, role));
        QCoreApplication::processEvents();
        return invoked && successes.size() == 1 && errors.isEmpty();
    }
    bool update(const QString &name, const QString &oldPassword, const QString &newName,
                const QString &password, int role)
    {
        clear();
        const bool invoked = QMetaObject::invokeMethod(store, "update", Qt::BlockingQueuedConnection,
            Q_ARG(QString, name), Q_ARG(QString, oldPassword), Q_ARG(QString, newName),
            Q_ARG(QString, password), Q_ARG(QString, password), Q_ARG(int, role));
        QCoreApplication::processEvents();
        return invoked && successes.size() == 1 && errors.isEmpty();
    }
    bool remove(const QString &name, const QString &password)
    {
        clear();
        const bool invoked = QMetaObject::invokeMethod(store, "remove", Qt::BlockingQueuedConnection,
            Q_ARG(QString, name), Q_ARG(QString, password));
        QCoreApplication::processEvents();
        return invoked && successes.size() == 1 && errors.isEmpty();
    }
    QJsonArray publicRows()
    {
        clear();
        QMetaObject::invokeMethod(store, "load", Qt::BlockingQueuedConnection);
        QCoreApplication::processEvents();
        QJsonArray result;
        if (rows.size() != 1 || !errors.isEmpty())
        {
            return result;
        }
        for (const AccountRow &row : qvariant_cast<AccountRows>(rows.first().at(0)))
        {
            result.append(QJsonObject{{QStringLiteral("id"), double(row.id)},
                {QStringLiteral("name"), row.name}, {QStringLiteral("role"), row.role},
                {QStringLiteral("isdefault"), row.isDefault}});
        }
        return result;
    }
    QString message() const
    {
        return errors.isEmpty() ? QString() : errors.last().at(1).toString();
    }
    QThread thread;
    AccountStore *store;
    QSignalSpy ready, errors, successes, sessions, rows;
};

bool findPublic(const QJsonArray &rows, const QString &name, int role)
{
    for (const QJsonValue &value : rows)
    {
        const auto row = value.toObject();
        if (row.value(QStringLiteral("name")) == name
                && row.value(QStringLiteral("role")).toInt() == role)
        {
            return true;
        }
    }
    return false;
}
}

class AccountBoundaryTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void noImplicitBootstrapOrReset();
    void rejectUntaggedAndInvalidTag();
    void rejectLinkedOrReferenceAlias();
    void rejectCorruptOrMismatchedSchemaUnchanged();
    void bindNamesAndReadSessionRole();
    void rejectForgedRoleAndStaleSession();
    void preserveAdministratorRoleZeroAndOriginalTargetRules();
    void cleanupTestCase();
private:
    QString directory(const char *name) const { return QDir(root_).filePath(QString::fromLatin1(name)); }
    void passed(const QString &name) { passed_.append(name); }
    QString root_;
    QJsonArray passed_;
};

void AccountBoundaryTest::initTestCase()
{
    AccountStore::registerTypes();
    root_ = QDir::cleanPath(qEnvironmentVariable("SN100_BOUNDARY_DIR"));
    QVERIFY2(QDir::isAbsolutePath(root_), "SN100_BOUNDARY_DIR must be a NEW absolute path under project temp");
    QVERIFY2(QDir::fromNativeSeparators(root_).contains(QStringLiteral("/temp/sn100_host/")),
        "Boundary fixtures must remain beneath temp/sn100_host");
    QVERIFY2(!QDir::fromNativeSeparators(root_).split(QLatin1Char('/'), Qt::SkipEmptyParts)
        .contains(QStringLiteral("sn100_host_reference"), Qt::CaseInsensitive),
        "Boundary fixture root must not name reference originals");
    QVERIFY2(!QFileInfo::exists(root_), "Existing boundary data is never removed or overwritten; choose a new root");
    QVERIFY(QDir().mkpath(root_));
}

void AccountBoundaryTest::noImplicitBootstrapOrReset()
{
    const QString path = directory("bootstrap");
    const QString first = secret(), replacement = secret();
    {
        Worker unseeded;
        QVERIFY(!unseeded.initialize(path));
        QVERIFY(unseeded.message().contains(QStringLiteral("首次")));
        QVERIFY(!QFileInfo::exists(databasePath(path)));
    }
    QJsonArray original;
    {
        Worker seeded;
        QVERIFY(seeded.initialize(path, first));
        original = seeded.publicRows();
        QCOMPARE(original.size(), 5);
    }
    const QByteArray before = bytes(databasePath(path));
    {
        Worker resetAttempt;
        QVERIFY(!resetAttempt.initialize(path, replacement));
        QVERIFY(resetAttempt.message().contains(QStringLiteral("禁止重新播种")));
    }
    // A boolean comparison avoids a failure dump of authentication file bytes.
    QVERIFY(bytes(databasePath(path)) == before);
    {
        Worker reopened;
        QVERIFY(reopened.initialize(path));
        QCOMPARE(reopened.publicRows(), original);
        QVERIFY(reopened.login(QStringLiteral("offlineEngineer")));
        QVERIFY(reopened.update(QStringLiteral("offlineEngineer"), first,
            QStringLiteral("offlineEngineer"), replacement, 4));
    }
    {
        Worker relative;
        QVERIFY(!relative.initialize(QStringLiteral("relative-account-boundary"), first));
        QVERIFY(relative.message().contains(QStringLiteral("绝对")));
    }
    passed(QStringLiteral("new DB requires explicit transient secret; existing DB cannot be reseeded; original secret remains usable; relative path rejected"));
}

void AccountBoundaryTest::rejectUntaggedAndInvalidTag()
{
    for (const QString &name : {QStringLiteral("user_Information.db"),
                               QStringLiteral("hospital_Information.db"), QStringLiteral("config.ini")})
    {
        const QString path = directory(qPrintable(QStringLiteral("untagged-") + name));
        QVERIFY(QDir().mkpath(path));
        const QString existing = QDir(path).filePath(name);
        const QByteArray sentinel("ANONYMOUS-UNOWNED-SENTINEL\n");
        QVERIFY(writeFile(existing, sentinel));
        Worker worker;
        QVERIFY(!worker.initialize(path, secret()));
        QVERIFY(worker.message().contains(QStringLiteral("缺少有效")));
        QVERIFY(bytes(existing) == sentinel);
        QVERIFY(!QFileInfo::exists(QDir(path).filePath(QStringLiteral("offline-data.tag"))));
    }
    const QString path = directory("invalid-tag");
    QVERIFY(QDir().mkpath(path));
    QVERIFY(writeFile(QDir(path).filePath(QStringLiteral("offline-data.tag")), QByteArray("NOT-AN-OFFLINE-TAG\n")));
    QVERIFY(writeFile(databasePath(path), QByteArray("ANONYMOUS-SENTINEL\n")));
    const QByteArray before = bytes(databasePath(path));
    Worker worker;
    QVERIFY(!worker.initialize(path));
    QVERIFY(worker.message().contains(QStringLiteral("无效")));
    QVERIFY(bytes(databasePath(path)) == before);
    passed(QStringLiteral("unmarked account/O01/config files and invalid tag rejected before modification"));
}

void AccountBoundaryTest::rejectLinkedOrReferenceAlias()
{
#ifndef Q_OS_UNIX
    QSKIP("Symlink boundaries are executed on the authorized WSL/Linux target only");
#else
    const QString path = directory("linked-db");
    QVERIFY(QDir().mkpath(path));
    QVERIFY(writeFile(QDir(path).filePath(QStringLiteral("offline-data.tag")), kTag));
    const QString target = directory("link-target-sentinel");
    const QByteArray sentinel("ANONYMOUS-LINK-TARGET-UNCHANGED\n");
    QVERIFY(writeFile(target, sentinel));
    QVERIFY(QFile::link(target, databasePath(path)));
    QVERIFY(QFileInfo(databasePath(path)).isSymLink());
    {
        Worker worker;
        QVERIFY(!worker.initialize(path));
        QVERIFY(worker.message().contains(QStringLiteral("符号链接")));
    }
    QVERIFY(bytes(target) == sentinel);
    const QString tagPath = directory("linked-tag");
    QVERIFY(QDir().mkpath(tagPath));
    const QString tagTarget = directory("tag-target");
    QVERIFY(writeFile(tagTarget, kTag));
    QVERIFY(QFile::link(tagTarget, QDir(tagPath).filePath(QStringLiteral("offline-data.tag"))));
    {
        Worker worker;
        QVERIFY(!worker.initialize(tagPath, secret()));
        QVERIFY(worker.message().contains(QStringLiteral("符号链接")));
    }
    QVERIFY(!QFileInfo::exists(databasePath(tagPath)));
    // The alias points only at a synthetic reference-named directory in temp.
    // Even a failing guard can never write the actual reference originals.
    const QString reference = directory("sandbox/sn100_host_reference");
    const QString alias = directory("reference-alias");
    QVERIFY(QDir().mkpath(reference));
    QVERIFY(QFile::link(reference, alias));
    const QString missing = QDir(alias).filePath(QStringLiteral("never-create"));
    {
        Worker worker;
        QVERIFY(!worker.initialize(missing, secret()));
        QVERIFY(worker.message().contains(QStringLiteral("参考工程")));
    }
    QVERIFY(!QFileInfo::exists(QDir(reference).filePath(QStringLiteral("never-create"))));
    passed(QStringLiteral("DB/tag symlinks and canonical reference-directory alias rejected without modifying targets or creating descendants"));
#endif
}

void AccountBoundaryTest::rejectCorruptOrMismatchedSchemaUnchanged()
{
    const QString corrupt = directory("corrupt");
    QVERIFY(QDir().mkpath(corrupt));
    QVERIFY(writeFile(QDir(corrupt).filePath(QStringLiteral("offline-data.tag")), kTag));
    const QByteArray invalid("THIS-IS-AN-ANONYMOUS-CORRUPT-DATABASE\n");
    QVERIFY(writeFile(databasePath(corrupt), invalid));
    {
        Worker worker;
        QVERIFY(!worker.initialize(corrupt));
        QVERIFY(!worker.message().isEmpty());
    }
    QVERIFY(bytes(databasePath(corrupt)) == invalid);

    const QStringList declarations = {
        QStringLiteral("CREATE TABLE user_information(id INTEGER PRIMARY KEY,name VARCHAR,role INTEGER)"),
        QStringLiteral("CREATE TABLE user_information(id INTEGER PRIMARY KEY,name TEXT,role INTEGER,pwd VARCHAR,isdefault INTEGER)"),
        QStringLiteral("CREATE TABLE unrelated(sentinel VARCHAR)")};
    for (int index = 0; index < declarations.size(); ++index)
    {
        const QString path = directory(qPrintable(QStringLiteral("schema-%1").arg(index)));
        QVERIFY(QDir().mkpath(path));
        QVERIFY(writeFile(QDir(path).filePath(QStringLiteral("offline-data.tag")), kTag));
        QVERIFY(createSqlite(databasePath(path), {declarations.at(index)}));
        const QByteArray before = bytes(databasePath(path));
        {
            Worker worker;
            QVERIFY(!worker.initialize(path));
            QVERIFY(worker.message().contains(QStringLiteral("账户表")));
        }
        QVERIFY(bytes(databasePath(path)) == before);
        QVERIFY(!QFileInfo::exists(databasePath(path) + QStringLiteral("-journal")));
    }
    passed(QStringLiteral("corrupt DB and incomplete/type-mismatched/missing account schema rejected without changing existing bytes"));
}

void AccountBoundaryTest::bindNamesAndReadSessionRole()
{
    Worker worker;
    const QString transient = secret();
    QVERIFY(worker.initialize(directory("bound-name"), transient));
    QVERIFY(worker.login(QStringLiteral("offlineEngineer")));
    const QString apostrophe = QStringLiteral("A'离线绑定");
    QVERIFY(worker.add(apostrophe, transient, 2));
    const QJsonArray rows = worker.publicRows();
    QCOMPARE(rows.size(), 6);
    QVERIFY(findPublic(rows, apostrophe, 2));
    QVERIFY(worker.login(apostrophe));
    QCOMPARE(worker.session().name, apostrophe);
    QCOMPARE(worker.session().role, 2);
    QVERIFY(worker.session().authenticated);
    QVERIFY(worker.update(apostrophe, transient, apostrophe, transient, 1));
    QVERIFY(worker.login(apostrophe));
    QCOMPARE(worker.session().role, 1);
    QVERIFY(!worker.login(QStringLiteral("not-an-existing-anonymous-account")));
    QVERIFY(!worker.session().authenticated);
    QVERIFY(worker.message().contains(QStringLiteral("不存在")));
    passed(QStringLiteral("ASCII-leading apostrophe/Chinese name bound as data; session role read from stored record; nonexistent session clears identity"));
}

void AccountBoundaryTest::rejectForgedRoleAndStaleSession()
{
    const QString path = directory("forged-role");
    Worker worker;
    const QString transient = secret();
    QVERIFY(worker.initialize(path, transient));
    QVERIFY(worker.login(QStringLiteral("offlineSend")));
    const QJsonArray original = worker.publicRows();
    for (int role : {-1, 0, 2, 5, 99})
    {
        QVERIFY(!worker.add(QStringLiteral("forgedRole"), transient, role));
        QVERIFY(worker.message().contains(QStringLiteral("角色")));
        QCOMPARE(worker.publicRows(), original);
    }
    QVERIFY(!worker.update(QStringLiteral("offlineSend"), transient,
        QStringLiteral("offlineSend"), transient, 5));
    QVERIFY(worker.message().contains(QStringLiteral("角色")));
    QCOMPARE(worker.publicRows(), original);
    QVERIFY(worker.login(QString()));
    QVERIFY(!worker.add(QStringLiteral("noSession"), transient, 1));
    QVERIFY(worker.message().contains(QStringLiteral("会话")));
    QCOMPARE(worker.publicRows(), original);
    // A real DB role change between selection and action must be re-read by
    // refreshSession, rather than trusting a stale role snapshot in the UI.
    QVERIFY(worker.login(QStringLiteral("offlineEngineer")));
    QVERIFY(createSqlite(databasePath(path), {QStringLiteral("UPDATE user_information SET role=1 WHERE name='offlineEngineer'")}));
    QVERIFY(!worker.add(QStringLiteral("staleRank"), transient, 4));
    QVERIFY(worker.message().contains(QStringLiteral("角色")));
    QVERIFY(worker.login(QStringLiteral("offlineEngineer")));
    QCOMPARE(worker.session().role, 1);
    passed(QStringLiteral("forged roles and no-session writes rejected; per-operation DB role refresh prevents stale simulated rank"));
}

void AccountBoundaryTest::preserveAdministratorRoleZeroAndOriginalTargetRules()
{
    const QString path = directory("source-ambiguities");
    Worker worker;
    const QString transient = secret();
    QVERIFY(worker.initialize(path, transient));
    QVERIFY(worker.login(QStringLiteral("offlineSend")));
    // The original update handler has no target-rank restriction. The chosen
    // replacement role still belongs to the actor's real combo options.
    QVERIFY(worker.update(QStringLiteral("offlineCheck"), transient,
        QStringLiteral("offlineCheck"), transient, 1));
    QVERIFY(findPublic(worker.publicRows(), QStringLiteral("offlineCheck"), 1));
    QVERIFY(worker.login(QStringLiteral("offlineEngineer")));
    // Preserve the source's missing duplicate-new-name check and existing
    // isdefault field; these are isolated ambiguities, not production policy.
    QVERIFY(createSqlite(databasePath(path), {QStringLiteral("UPDATE user_information SET isdefault=1 WHERE name='offlineTest'")}));
    QVERIFY(worker.update(QStringLiteral("offlineTest"), transient,
        QStringLiteral("offlineSend"), QString(), 1));
    int duplicated = 0;
    bool preservedDefault = false;
    for (const QJsonValue &value : worker.publicRows())
    {
        const auto row = value.toObject();
        if (row.value(QStringLiteral("name")) == QStringLiteral("offlineSend"))
        {
            ++duplicated;
            preservedDefault |= row.value(QStringLiteral("isdefault")).toInt() == 1;
        }
    }
    QCOMPARE(duplicated, 2);
    QVERIFY(preservedDefault);
    QVERIFY(worker.login(QStringLiteral("offlineAdmin")));
    QVERIFY(worker.update(QStringLiteral("offlineAdmin"), transient,
        QStringLiteral("offlineAdmin"), transient, 0));
    QVERIFY(!worker.successes.isEmpty());
    QVERIFY(worker.successes.last().at(1).toBool());
    QVERIFY(!worker.session().authenticated);
    QVERIFY(findPublic(worker.publicRows(), QStringLiteral("offlineAdmin"), 0));
    QVERIFY(worker.login(QStringLiteral("offlineAdmin")));
    QCOMPARE(worker.session().role, 0);
    QVERIFY(worker.session().authenticated);
    QVERIFY(!worker.add(QStringLiteral("guestDeniedAfterSourceUpdate"), transient, 1));
    QVERIFY(worker.message().contains(QStringLiteral("无权限")));
    passed(QStringLiteral("source cross-target/duplicate-name/empty-new-password/isdefault rules and Administrator invalid-option role0 update preserved in disposable data"));
}

void AccountBoundaryTest::cleanupTestCase()
{
    if (root_.isEmpty() || !QFileInfo(root_).isDir())
    {
        return;
    }
    const QJsonObject result{{QStringLiteral("process_id"), double(QCoreApplication::applicationPid())},
        {QStringLiteral("qt_version"), QString::fromLatin1(qVersion())},
        {QStringLiteral("case_groups_completed"), passed_},
        {QStringLiteral("reference_source_or_production_database_read"), false},
        {QStringLiteral("reference_alias_target"), QStringLiteral("synthetic directory under the boundary temp root")},
        {QStringLiteral("credential_material_exported"), false}};
    QVERIFY(writeFile(QDir(root_).filePath(QStringLiteral("boundary_result.json")),
        QJsonDocument(result).toJson()));
}

QTEST_GUILESS_MAIN(AccountBoundaryTest)
#include "account_boundary_test.moc"
