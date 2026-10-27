#include <QtTest>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThread>
#include "data/hospital_store.h"
#include "data/hospital_dictionary_query.h"

namespace
{
QByteArray fileHash(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
}

class StoreSession
{
public:
    StoreSession()
    {
        HospitalStore::registerTypes();
        registerHospitalDictionaryTypes();
        store = new HospitalStore;
        store->moveToThread(&thread);
        QObject::connect(&thread, &QThread::finished, store, &QObject::deleteLater);
        thread.start();
    }
    ~StoreSession()
    {
        QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
        thread.quit();
        if (!thread.wait(5000)) qFatal("HospitalStore did not stop in teardown budget");
    }
    void initialize()
    {
        databasePath = QDir(temp.path()).filePath(QStringLiteral("hospital.sqlite"));
        iniPath = QDir(temp.path()).filePath(QStringLiteral("hospital.ini"));
        QMetaObject::invokeMethod(store, "initialize", Qt::QueuedConnection,
            Q_ARG(QString, databasePath), Q_ARG(QString, iniPath));
    }
    void query(const HospitalDictionaryRequest &request)
    {
        QMetaObject::invokeMethod(store, "queryCommittedDictionary", Qt::QueuedConnection,
                                  Q_ARG(HospitalDictionaryRequest, request));
    }
    QTemporaryDir temp;
    QThread thread;
    HospitalStore *store = nullptr;
    QString databasePath;
    QString iniPath;
};

HospitalDictionaryRequest request(const QString &type, const QString &filter = QString())
{
    return {QUuid::createUuid(), type, filter};
}
}

class ReportDictionaryStoreContractTest : public QObject
{
    Q_OBJECT
private slots:
    void filterLengthBoundaries_data()
    {
        QTest::addColumn<QString>("filter");
        QTest::addColumn<bool>("accepted");
        QTest::addColumn<int>("matches");
        QTest::newRow("empty") << QString() << true << 1;
        QTest::newRow("unicode-short") << QStringLiteral("短") << true << 1;
        QTest::newRow("ascii-33") << QString(kHospitalDictionaryFilterLimit, QLatin1Char('%')) << true << 1;
        QTest::newRow("unicode-33") << QString(kHospitalDictionaryFilterLimit, QChar(0x4e2d)) << true << 0;
        QTest::newRow("ascii-34-rejected-not-truncated") << QString(kHospitalDictionaryFilterLimit + 1, QLatin1Char('%')) << false << 0;
        QTest::newRow("unicode-34-rejected-not-truncated") << QString(kHospitalDictionaryFilterLimit + 1, QChar(0x4e2d)) << false << 0;
    }

    void filterLengthBoundaries()
    {
        QFETCH(QString, filter);
        QFETCH(bool, accepted);
        QFETCH(int, matches);
        StoreSession session;
        QSignalSpy ready(session.store, &HospitalStore::ready);
        QSignalSpy saved(session.store, &HospitalStore::saved);
        QSignalSpy replies(session.store, &HospitalStore::dictionaryQueryCompleted);
        QSignalSpy editorErrors(session.store, &HospitalStore::error);
        session.initialize();
        QTRY_COMPARE(ready.size(), 1);
        const HospitalRows seed = {{0, QStringLiteral("短名"), QStringLiteral("zw"),
                                    QStringLiteral("SNTL"), QStringLiteral("keshi")}};
        QMetaObject::invokeMethod(session.store, "save", Qt::QueuedConnection,
            Q_ARG(QString, QStringLiteral("keshi")), Q_ARG(QString, QStringLiteral("匿名测试医院")),
            Q_ARG(HospitalRows, seed));
        QTRY_COMPARE(saved.size(), 1);
        const QByteArray before = fileHash(session.databasePath);
        QVERIFY(!before.isEmpty());
        const auto original = request(QStringLiteral("keshi"), filter);
        session.query(original);
        QTRY_COMPARE(replies.size(), 1);
        const auto reply = qvariant_cast<HospitalDictionaryReply>(replies.first().at(0));
        QVERIFY(sameHospitalDictionaryRequest(reply.request, original));
        QCOMPARE(reply.request.filter, filter);
        QCOMPARE(reply.succeeded, accepted);
        QCOMPARE(reply.entries.size(), matches);
        if (!accepted)
        {
            QVERIFY(!reply.error.isEmpty());
            QVERIFY(reply.entries.isEmpty());
        }
        QCOMPARE(editorErrors.size(), 0);
        QCOMPARE(fileHash(session.databasePath), before);
    }

    void fiveTypesNameMnemonicLikeAndReadOnly()
    {
        StoreSession session;
        QSignalSpy ready(session.store, &HospitalStore::ready);
        QSignalSpy saved(session.store, &HospitalStore::saved);
        QSignalSpy replies(session.store, &HospitalStore::dictionaryQueryCompleted);
        QSignalSpy editorLoaded(session.store, &HospitalStore::loaded);
        QSignalSpy editorErrors(session.store, &HospitalStore::error);
        session.initialize();
        QTRY_COMPARE(ready.size(), 1);
        const QStringList types = {"keshi", "bingren", "jianyan", "songjian", "shenhe"};
        for (const QString &type : types)
        {
            HospitalRows rows = {{0, QStringLiteral("共同名称"), QStringLiteral("first1"),
                                   QStringLiteral("SNTL1"), type},
                                 {0, QStringLiteral("另一名称"), QStringLiteral("match2"),
                                   QStringLiteral("SNTL2"), type}};
            QMetaObject::invokeMethod(session.store, "save", Qt::QueuedConnection,
                Q_ARG(QString, type), Q_ARG(QString, QStringLiteral("匿名测试医院")), Q_ARG(HospitalRows, rows));
        }
        QTRY_COMPARE(saved.size(), 5);
        // 仅在新建匿名库构造历史兼容行。当前字典编辑规则不允许引号等名称；
        // 不能为测试放宽正式save校验，也不能把这些值伪装为编辑页可新建数据。
        const QString legacyFullName = QStringLiteral("历史审核完整名称").repeated(8);
        QVERIFY(legacyFullName.size() > kHospitalDictionaryFilterLimit);
        const QString fixtureConnection = QUuid::createUuid().toString();
        {
            auto fixture = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), fixtureConnection);
            fixture.setDatabaseName(session.databasePath);
            QVERIFY(fixture.open());
            QSqlQuery insertLegacy(fixture);
            QVERIFY(insertLegacy.prepare(QStringLiteral(
                "UPDATE hospital_information SET column_one=? WHERE type=? AND column_two=?")));
            insertLegacy.addBindValue(QStringLiteral("O'Brien_%中"));
            insertLegacy.addBindValue(QStringLiteral("keshi"));
            insertLegacy.addBindValue(QStringLiteral("match2"));
            QVERIFY(insertLegacy.exec());
            QCOMPARE(insertLegacy.numRowsAffected(), qint64(1));
            insertLegacy.finish();
            QVERIFY(insertLegacy.prepare(QStringLiteral(
                "UPDATE hospital_information SET column_one=? WHERE type=? AND column_two=?")));
            insertLegacy.addBindValue(legacyFullName);
            insertLegacy.addBindValue(QStringLiteral("shenhe"));
            insertLegacy.addBindValue(QStringLiteral("match2"));
            QVERIFY(insertLegacy.exec());
            QCOMPARE(insertLegacy.numRowsAffected(), qint64(1));
            insertLegacy.finish();
            fixture.close();
        }
        QSqlDatabase::removeDatabase(fixtureConnection);
        const QByteArray databaseBefore = fileHash(session.databasePath);
        const QByteArray iniBefore = fileHash(session.iniPath);
        QVERIFY(!databaseBefore.isEmpty() && !iniBefore.isEmpty());
        for (const QString &type : types)
        {
            const auto query = request(type);
            const int expected = replies.size() + 1;
            session.query(query);
            QTRY_COMPARE(replies.size(), expected);
            const auto reply = qvariant_cast<HospitalDictionaryReply>(replies.last().at(0));
            QVERIFY(sameHospitalDictionaryRequest(reply.request, query));
            QVERIFY2(reply.succeeded, qPrintable(reply.error));
            QCOMPARE(reply.entries.size(), 2);
            QCOMPARE(reply.entries.first().name, QStringLiteral("共同名称"));
            const QString expectedLastName = type == QStringLiteral("keshi")
                    ? QStringLiteral("O'Brien_%中") : type == QStringLiteral("shenhe")
                      ? legacyFullName : QStringLiteral("另一名称");
            QCOMPARE(reply.entries.last().name, expectedLastName);
            QVERIFY(reply.entries.first().id < reply.entries.last().id);
            const auto committed = qvariant_cast<HospitalRows>(saved.at(types.indexOf(type)).at(2));
            QCOMPARE(reply.entries.first().id, committed.first().id);
            QCOMPARE(reply.entries.last().id, committed.last().id);
        }
        const QList<QPair<QString, int>> filters = {
            {QStringLiteral("共同"), 1}, {QStringLiteral("match2"), 1},
            {QStringLiteral("O'Brien"), 1}, {QStringLiteral("%"), 2},
            {QStringLiteral("_"), 2}, {QStringLiteral("' OR 1=1 --"), 0},
            {QStringLiteral("SNTL2"), 0}, {QStringLiteral("不存在"), 0}, {QStringLiteral("未提交"), 0}};
        for (const auto &test : filters)
        {
            const int expected = replies.size() + 1;
            session.query(request(QStringLiteral("keshi"), test.first));
            QTRY_COMPARE(replies.size(), expected);
            const auto reply = qvariant_cast<HospitalDictionaryReply>(replies.last().at(0));
            QVERIFY2(reply.succeeded, qPrintable(reply.error));
            QCOMPARE(reply.entries.size(), test.second);
        }
        QCOMPARE(fileHash(session.databasePath), databaseBefore);
        QCOMPARE(fileHash(session.iniPath), iniBefore);
        QCOMPARE(editorLoaded.size(), 0);
        QCOMPARE(editorErrors.size(), 0);
        auto draft = qvariant_cast<HospitalRows>(saved.at(1).at(2));
        draft[0].columnOne = QStringLiteral("未提交");
        const int beforeCommit = replies.size();
        session.query(request(QStringLiteral("bingren"), QStringLiteral("未提交")));
        QTRY_COMPARE(replies.size(), beforeCommit + 1);
        QVERIFY(qvariant_cast<HospitalDictionaryReply>(replies.last().at(0)).entries.isEmpty());
        QMetaObject::invokeMethod(session.store, "save", Qt::QueuedConnection,
            Q_ARG(QString, QStringLiteral("bingren")), Q_ARG(QString, QStringLiteral("匿名测试医院")),
            Q_ARG(HospitalRows, draft));
        QTRY_COMPARE(saved.size(), 6);
        session.query(request(QStringLiteral("bingren"), QStringLiteral("未提交")));
        QTRY_COMPARE(replies.size(), beforeCommit + 2);
        const auto committedReply = qvariant_cast<HospitalDictionaryReply>(replies.last().at(0));
        QVERIFY(committedReply.succeeded);
        QCOMPARE(committedReply.entries.size(), 1);
        QCOMPARE(committedReply.entries.first().id, draft.first().id);
    }

    void failuresAlwaysCorrelatedAndDoNotFinishEditorRequests()
    {
        StoreSession session;
        QSignalSpy replies(session.store, &HospitalStore::dictionaryQueryCompleted);
        QSignalSpy editorErrors(session.store, &HospitalStore::error);
        const auto unready = request(QStringLiteral("keshi"));
        session.query(unready);
        QTRY_COMPARE(replies.size(), 1);
        auto reply = qvariant_cast<HospitalDictionaryReply>(replies.last().at(0));
        QVERIFY(sameHospitalDictionaryRequest(reply.request, unready));
        QVERIFY(!reply.succeeded && !reply.error.isEmpty() && reply.entries.isEmpty());
        QSignalSpy ready(session.store, &HospitalStore::ready);
        session.initialize();
        QTRY_COMPARE(ready.size(), 1);
        const auto invalid = request(QStringLiteral("not-a-dictionary"));
        session.query(invalid);
        QTRY_COMPARE(replies.size(), 2);
        reply = qvariant_cast<HospitalDictionaryReply>(replies.last().at(0));
        QVERIFY(sameHospitalDictionaryRequest(reply.request, invalid));
        QVERIFY(!reply.succeeded && !reply.error.isEmpty() && reply.entries.isEmpty());
        const QString connection = QUuid::createUuid().toString();
        {
            auto blocker = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
            blocker.setDatabaseName(session.databasePath);
            QVERIFY(blocker.open());
            QSqlQuery lock(blocker);
            QVERIFY(lock.exec(QStringLiteral("BEGIN EXCLUSIVE")));
            const auto locked = request(QStringLiteral("keshi"));
            session.query(locked);
            QTRY_COMPARE(replies.size(), 3);
            reply = qvariant_cast<HospitalDictionaryReply>(replies.last().at(0));
            QVERIFY(sameHospitalDictionaryRequest(reply.request, locked));
            QVERIFY(!reply.succeeded && !reply.error.isEmpty() && reply.entries.isEmpty());
            QVERIFY(lock.exec(QStringLiteral("ROLLBACK")));
            lock.finish();
            blocker.close();
        }
        QSqlDatabase::removeDatabase(connection);
        const auto retry = request(QStringLiteral("keshi"));
        session.query(retry);
        QTRY_COMPARE(replies.size(), 4);
        reply = qvariant_cast<HospitalDictionaryReply>(replies.last().at(0));
        QVERIFY(sameHospitalDictionaryRequest(reply.request, retry));
        QVERIFY(reply.succeeded && reply.entries.isEmpty());
        QCOMPARE(editorErrors.size(), 0);
    }
};

QTEST_GUILESS_MAIN(ReportDictionaryStoreContractTest)
#include "report_dictionary_store_contract_test.moc"
