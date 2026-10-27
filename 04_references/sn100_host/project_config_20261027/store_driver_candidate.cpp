#include "projects_store.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QSqlError>
#include <QThread>
#include <QTimer>
#include <QDebug>
#include <functional>
#include <limits>

// 只在RK执行。等待循环属于验证驱动，不进入正式页面的业务回调。
static bool awaitResult(const std::function<bool()> &done, int timeout = 5000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!done() && elapsed.elapsed() < timeout)
    {
        QEventLoop loop;
        QTimer::singleShot(10, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return done();
}

class Verification
{
public:
    int failures = 0;
    int passed = 0;
    void check(bool result, const QString &name)
    {
        qInfo().noquote() << (result ? "PASS" : "FAIL") << name;
        result ? ++passed : ++failures;
    }
};

static ProjectRecord inputProject(const QString &name)
{
    ProjectRecord value;
    value.instrumentProjectId = QStringLiteral("ANONYMOUS_CONFIG_ONLY");
    value.name = name;
    value.fullName = QStringLiteral("匿名配置核对");
    int id = 0;
    for (const QString &type : {QStringLiteral("Original"), QStringLiteral("Calibration"),
                               QStringLiteral("Rate"), QStringLiteral("INR")})
    {
        ProjectResult row;
        row.type = type;
        row.name = name + QString::number(++id);
        row.unit = QStringLiteral("s");
        row.mainResult = id == 1;
        row.selectionKnown = true;
        row.sourceResultId = id;
        row.used = id == 1;
        row.display = id == 1;
        value.results.append(row);
    }
    return value;
}

static QJsonObject jsonProject(const ProjectRecord &value)
{
    QJsonArray results;
    for (const auto &row : value.results)
        results.append(QJsonObject{{"type", row.type}, {"name", row.name}, {"decimals", row.decimals},
            {"testLow", row.testLow}, {"testHigh", row.testHigh}, {"normalLow", row.normalLow},
            {"normalHigh", row.normalHigh}, {"lisCode", row.lisCode}, {"unit", row.unit},
            {"mainResult", row.mainResult}, {"selectionKnown", row.selectionKnown},
            {"sourceResultId", row.sourceResultId}, {"used", row.used}, {"display", row.display}});
    return {{"instrumentProjectId", value.instrumentProjectId}, {"name", value.name},
        {"fullName", value.fullName}, {"lisCode", value.lisCode}, {"enabled", value.enabled},
        {"results", results}, {"sampleRates", QJsonArray()}, {"advanced", QJsonValue()},
        {"algorithm", QJsonValue()}};
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 3 || QString::fromLocal8Bit(argv[1]) != QStringLiteral("--data-dir")) return 2;
    const QString root = QString::fromLocal8Bit(argv[2]);
    // 驱动只接受新的绝对目录，保留现场数据；重开用同一次驱动内的close/initialize。
    if (!QDir::isAbsolutePath(root) || QFileInfo::exists(root)) return 2;
    if (!QDir().mkpath(root)) return 2;
    Verification verify;
    ProjectsStore::registerTypes();
    QThread worker;
    auto *store = new ProjectsStore;
    store->moveToThread(&worker);
    QObject::connect(&worker, &QThread::finished, store, &QObject::deleteLater);
    qint64 requested = 10000;
    qint64 receipt = -1;
    qint64 savedId = 0;
    bool succeeded = false;
    bool ready = false;
    ProjectRecords projects;
    ProjectUnits units;
    QString databasePath;
    QObject::connect(store, &ProjectsStore::ready, &app, [&](const QString &path)
    { ready = true; databasePath = path; qInfo().noquote() << "DATABASE" << path; });
    QObject::connect(store, &ProjectsStore::succeeded, &app,
        [&](qint64 id, const QString &operation, qint64 projectId)
        { receipt = id; succeeded = true; savedId = projectId; qInfo() << "RECEIPT" << id << operation << projectId; });
    QObject::connect(store, &ProjectsStore::error, &app,
        [&](qint64 id, const QString &operation, const QString &message)
        { receipt = id; succeeded = false; qInfo().noquote() << "REJECTED" << id << operation << message; });
    QObject::connect(store, &ProjectsStore::snapshotReady, &app,
        [&](qint64 id, const ProjectRecords &p, const ProjectUnits &u)
        { receipt = id; succeeded = true; projects = p; units = u; });
    QObject::connect(store, &ProjectsStore::projectReferenceSucceeded, &app,
        [&](qint64 id, const QString &) { receipt = id; succeeded = true; });
    QObject::connect(store, &ProjectsStore::projectReferenceError, &app,
        [&](qint64 id, const QString &, const QString &) { receipt = id; succeeded = false; });
    worker.start();
    auto queue = [&](const std::function<void()> &operation)
    { QMetaObject::invokeMethod(store, operation, Qt::QueuedConnection); };
    auto expect = [&](bool expected, const QString &name)
    {
        const bool arrived = awaitResult([&]() { return receipt == requested; });
        verify.check(arrived && succeeded == expected, name);
    };
    auto load = [&]()
    {
        const qint64 id = ++requested;
        queue([=]() { store->load(id); });
        expect(true, QStringLiteral("读取实际资料"));
    };
    auto addUnit = [&](const ProjectUnit &unit, bool expected)
    {
        const qint64 id = ++requested;
        queue([=]() { store->addUnit(id, unit); });
        expect(expected, QStringLiteral("单位录入：") + unit.name);
    };
    auto create = [&](const ProjectRecord &value, bool expected)
    {
        const qint64 id = ++requested;
        queue([=]() { store->create(id, value); });
        expect(expected, QStringLiteral("新增项目：") + value.name);
    };
    auto save = [&](const ProjectRecord &value, bool expected)
    {
        const qint64 id = ++requested;
        queue([=]() { store->save(id, value); });
        expect(expected, QStringLiteral("保存项目：") + value.name);
    };
    queue([=]() { store->initialize(root, false); });
    verify.check(awaitResult([&]() { return ready; }), QStringLiteral("新匿名空库初始化，无演示种子"));
    AccountSession session;
    session.name = QStringLiteral("rkConfigEngineer");
    session.role = 4;
    session.authenticated = true;
    session.source = AccountSessionSource::Simulated;
    queue([=]() { store->setSession(session); });
    load();
    verify.check(projects.isEmpty() && units.isEmpty(), QStringLiteral("项目与单位确实为空"));
    addUnit({QStringLiteral("s"), 1}, true);
    addUnit({QStringLiteral("s"), 1}, false);
    addUnit({QStringLiteral("invalid"), 0}, false);
    auto value = inputProject(QStringLiteral("ANON_A"));
    auto invalid = value;
    invalid.results[0].unit = QStringLiteral("missing");
    create(invalid, false);
    create(value, true);
    const qint64 originalId = savedId;
    load();
    verify.check(projects.size() == 1 && projects.first().id == originalId,
                 QStringLiteral("新增保存后读回，未用SQL造记录"));
    value = projects.value(0);
    value.algorithm = {true, -1.25, 2.5, 3.75, true};
    save(value, true);
    load();
    verify.check(projects.value(0).algorithm.configured && projects.value(0).algorithm.coefficientA == -1.25,
                 QStringLiteral("算法参数实际保存读回"));
    invalid = value;
    invalid.algorithm.coefficientA = std::numeric_limits<double>::quiet_NaN();
    save(invalid, false);
    {
        const qint64 id = ++requested;
        queue([=]() { store->copy(id, originalId, QStringLiteral("ANON_COPY")); });
        expect(true, QStringLiteral("复制产生独立项目"));
    }
    const qint64 copyId = savedId;
    load();
    for (auto row : projects) if (row.id == copyId)
    { row.fullName = QStringLiteral("副本独立修改"); save(row, true); }
    load();
    for (const auto &row : projects) if (row.id == originalId)
        verify.check(row.fullName == value.fullName, QStringLiteral("改副本不改变原项目"));
    {
        const qint64 id = ++requested;
        queue([=]() { store->holdProjectReference(id, "rk_integration", "anonymous_ref", originalId); });
        expect(true, QStringLiteral("登记真实业务引用"));
    }
    {
        const qint64 id = ++requested;
        queue([=]() { store->remove(id, originalId); });
        expect(false, QStringLiteral("被跨库引用的项目拒绝删除"));
    }
    {
        const qint64 id = ++requested;
        queue([=]() { store->removeUnit(id, "s"); });
        expect(false, QStringLiteral("被引用单位拒绝删除"));
    }
    AccountSession guest;
    queue([=]() { store->setSession(guest); });
    addUnit({QStringLiteral("guest"), 1}, false);
    queue([=]() { store->setSession(session); });
    // 只用另一个连接占用写锁，触发真实写入失败；不注入任何业务记录。
    {
        auto lock = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("rk_write_lock"));
        lock.setDatabaseName(databasePath);
        verify.check(lock.open(), QStringLiteral("建立写失败用连接"));
        QSqlQuery transaction(lock);
        verify.check(transaction.exec(QStringLiteral("BEGIN IMMEDIATE")), QStringLiteral("占用数据库写锁"));
        addUnit({QStringLiteral("recoverable"), 1}, false);
        verify.check(transaction.exec(QStringLiteral("ROLLBACK")), QStringLiteral("释放写锁"));
        lock.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("rk_write_lock"));
    addUnit({QStringLiteral("recoverable"), 1}, true);
    {
        const qint64 id = ++requested;
        queue([=]() { store->updateUnit(id, "recoverable", {"unused_changed", 10}); });
        expect(true, QStringLiteral("未引用单位允许改名和倍率"));
    }
    {
        const qint64 id = ++requested;
        queue([=]() { store->removeUnit(id, "unused_changed"); });
        expect(true, QStringLiteral("未引用单位允许删除"));
    }
    addUnit({QStringLiteral("recoverable"), 1}, true);
    const QString importPath = root + QStringLiteral("/anonymous_import.json");
    const QJsonObject document{{"format", "sn100-project-config-v1"}, {"units", QJsonArray()},
        {"projects", QJsonArray{jsonProject(inputProject(QStringLiteral("ANON_IMPORTED")))}}};
    QFile file(importPath);
    verify.check(file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(document).toJson()) > 0,
                 QStringLiteral("写入显式匿名导入文件"));
    file.close();
    {
        const qint64 id = ++requested;
        queue([=]() { store->importFile(id, importPath); });
        expect(true, QStringLiteral("真实JSON文件导入"));
    }
    load();
    verify.check(projects.size() == 3, QStringLiteral("导入后项目数量为3"));
    {
        const qint64 id = ++requested;
        queue([=]() { store->importFile(id, importPath); });
        expect(false, QStringLiteral("重复导入拒绝覆盖"));
    }
    const QString rollbackPath = root + QStringLiteral("/rollback_import.json");
    QFile rollbackFile(rollbackPath);
    verify.check(rollbackFile.open(QIODevice::WriteOnly) && rollbackFile.write(QJsonDocument(QJsonObject{
        {"format", "sn100-project-config-v1"}, {"units", QJsonArray{QJsonObject{{"name", "must_not_remain"}, {"ratio", 1}}}},
        {"projects", QJsonArray{jsonProject(inputProject("ANON_IMPORTED"))}}}).toJson()) > 0,
        QStringLiteral("准备包含合法单位和重复项目的混合文件"));
    rollbackFile.close();
    {
        const qint64 id = ++requested;
        queue([=]() { store->importFile(id, rollbackPath); });
        expect(false, QStringLiteral("混合文件整批拒绝"));
    }
    load();
    verify.check(units.size() == 2 && projects.size() == 3, QStringLiteral("整批失败没有留下先处理的单位"));
    QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
    ready = false;
    queue([=]() { store->initialize(root, false); store->setSession(session); });
    verify.check(awaitResult([&]() { return ready; }), QStringLiteral("同目录无种子重开"));
    load();
    verify.check(projects.size() == 3 && units.size() == 2, QStringLiteral("重开后项目与单位一致"));
    {
        const auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("rk_readback"));
        QSqlDatabase audit = db;
        audit.setDatabaseName(databasePath);
        audit.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        verify.check(audit.open(), QStringLiteral("独立只读数据库核对"));
        QSqlQuery query(audit);
        verify.check(query.exec(QStringLiteral("PRAGMA integrity_check")) && query.next() && query.value(0).toString() == "ok",
                     QStringLiteral("数据库完整性核对"));
        audit.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("rk_readback"));
    QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
    ready = false;
    verify.check(QFile::setPermissions(databasePath, QFileDevice::ReadOwner) &&
        QFile::setPermissions(root, QFileDevice::ReadOwner | QFileDevice::ExeOwner), QStringLiteral("设置当前匿名数据根只读"));
    receipt = -1;
    queue([=]() { store->initialize(root, false); });
    verify.check(awaitResult([&]() { return receipt == 0; }) && !ready && !succeeded,
                 QStringLiteral("只读目录初始化清楚失败，不假装已打开可写库"));
    verify.check(QFile::setPermissions(root, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner) &&
        QFile::setPermissions(databasePath, QFileDevice::ReadOwner | QFileDevice::WriteOwner), QStringLiteral("恢复匿名数据目录权限"));
    queue([=]() { store->initialize(root, false); store->setSession(session); });
    verify.check(awaitResult([&]() { return ready; }), QStringLiteral("恢复权限后重新打开"));
    load();
    verify.check(projects.size() == 3 && units.size() == 2, QStringLiteral("权限失败恢复后原数据完整"));
    QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
    // 只有固定保护这一项使用显式旧匿名夹具；不把它当新建或导入成功。
    ready = false;
    queue([=]() { store->initialize(root + "/fixed_protection_fixture", true); store->setSession(session); });
    verify.check(awaitResult([&]() { return ready; }), QStringLiteral("固定保护专用旧匿名夹具"));
    load();
    verify.check(!projects.isEmpty() && projects.first().fixed, QStringLiteral("确认专用夹具是固定项目"));
    if (!projects.isEmpty())
    {
        const qint64 fixedId = projects.first().id;
        const qint64 id = ++requested;
        queue([=]() { store->remove(id, fixedId); });
        expect(false, QStringLiteral("固定项目删除被拒绝"));
    }
    QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
    worker.quit();
    verify.check(worker.wait(3000), QStringLiteral("数据库线程正常退出"));
    qInfo() << "RESULT" << "passed" << verify.passed << "failed" << verify.failures
            << "instrumentActions" << 0 << "backendOnly" << true;
    return verify.failures ? 1 : 0;
}
