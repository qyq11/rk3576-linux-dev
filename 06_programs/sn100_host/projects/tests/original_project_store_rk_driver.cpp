#include "projects_store.h"
#include "original_project_repository.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QDebug>
#include <algorithm>
#include <functional>

// 此驱动只在RK执行。测试根还须带专用标记，不能误操作供用户查看的工作副本。
// SQL写入仅用于下方明确标记的匿名导入夹具准备与排它锁故障注入；功能写入全部调用正式Store。
namespace
{
bool awaitResult(const std::function<bool()> &done, int timeout = 8000)
{
    QElapsedTimer elapsed; elapsed.start();
    while (!done() && elapsed.elapsed() < timeout)
    {
        QEventLoop loop;
        QTimer::singleShot(5, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return done();
}
struct AuditConnection
{
    QSqlDatabase db;
    QString name;
    ~AuditConnection() { if (!name.isEmpty()) { db.close(); db = QSqlDatabase(); QSqlDatabase::removeDatabase(name); } }
    bool open(const QString &path, bool writable = false)
    {
        name = QStringLiteral("OriginalAudit-") + QUuid::createUuid().toString();
        db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        db.setConnectOptions(writable ? QStringLiteral("QSQLITE_BUSY_TIMEOUT=1000") : QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=1000"));
        return db.open();
    }
};
using Rows = QList<QMap<QString, QVariant>>;
Rows tableRows(const QString &path, const QString &table, bool *ok = nullptr)
{
    if (ok) *ok = false;
    AuditConnection connection;
    if (!connection.open(path)) return {};
    QSqlQuery query(connection.db);
    if (!query.exec(QStringLiteral("SELECT rowid,* FROM \"%1\" ORDER BY rowid").arg(table))) return {};
    const QSqlRecord record = query.record();
    Rows result;
    while (query.next())
    {
        QMap<QString, QVariant> row;
        for (int column = 0; column < record.count(); ++column) row.insert(record.fieldName(column), query.value(column));
        result.append(row);
    }
    if (ok) *ok = !query.lastError().isValid();
    return result;
}
QString tableDigest(const QString &path, const QString &table)
{
    bool ok = false;
    const Rows rows = tableRows(path, table, &ok);
    if (!ok) return {};
    QByteArray bytes; QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_12); stream << rows;
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QString schemaDigest(const QString &path)
{
    AuditConnection connection;
    if (!connection.open(path)) return {};
    QSqlQuery query(connection.db);
    if (!query.exec(QStringLiteral("SELECT type,name,tbl_name,sql FROM sqlite_master ORDER BY type,name"))) return {};
    QByteArray bytes; QDataStream stream(&bytes, QIODevice::WriteOnly);
    while (query.next()) for (int index = 0; index < 4; ++index) stream << query.value(index);
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
bool quickCheck(const QString &path)
{
    AuditConnection connection; if (!connection.open(path)) return false;
    QSqlQuery query(connection.db);
    return query.exec(QStringLiteral("PRAGMA quick_check")) && query.next() && query.value(0).toString() == QStringLiteral("ok");
}
QString hash(const QString &path)
{
    QString error; return OriginalProjectRepository::databaseFingerprint(path, &error);
}
QString projectPath(const QString &root, const ProjectRecord &project)
{ return QDir(root).filePath(QStringLiteral("project/") + project.originalFileName); }
ProjectRecord findProject(const ProjectRecords &projects, qint64 id)
{ for (const auto &project : projects) if (project.id == id) return project; return {}; }
ProjectUnit findUnit(const ProjectUnits &units, const QString &name)
{ for (const auto &unit : units) if (unit.name == name) return unit; return {}; }
bool copyConfiguration(const QString &source, const QString &destination)
{
    if (QFileInfo::exists(destination)) return false;
    if (!QDir().mkpath(destination + QStringLiteral("/project")) || !QDir().mkpath(destination + QStringLiteral("/db/systemInfo")) || !QDir().mkpath(destination + QStringLiteral("/db/userSet"))) return false;
    QStringList files = {QStringLiteral(".sn100-original-work-v1"), QStringLiteral("db/systemInfo/option_Unit.db"), QStringLiteral("db/userSet/reagent_Information.db")};
    for (const QString &name : QDir(source + QStringLiteral("/project")).entryList({QStringLiteral("*.db")}, QDir::Files)) files.append(QStringLiteral("project/") + name);
    for (const QString &relative : files) if (!QFile::copy(QDir(source).filePath(relative), QDir(destination).filePath(relative))) return false;
    return true;
}
// 独立匿名夹具：仅替换项目身份/结果标签，保留原七张表的其他参数；不修改参考资料或正式工作副本。
bool prepareImportFixture(const QString &source, const QString &destination)
{
    if (!QFile::copy(source, destination)) return false;
    AuditConnection connection;
    if (!connection.open(destination, true) || !connection.db.transaction()) return false;
    const QMap<QString, QString> replacements = {{QStringLiteral("xuhao"), QStringLiteral("8999")}, {QStringLiteral("project_en_name"), QStringLiteral("RK_ORIGINAL_IMPORT")},
        {QStringLiteral("project_cn_name"), QStringLiteral("匿名导入核对")}, {QStringLiteral("fixed"), QStringLiteral("0")}, {QStringLiteral("is_related"), QStringLiteral("0")}};
    for (auto field = replacements.cbegin(); field != replacements.cend(); ++field)
    {
        QSqlQuery query(connection.db); query.prepare(QStringLiteral("UPDATE project_base SET value=? WHERE name=?"));
        query.addBindValue(field.value()); query.addBindValue(field.key()); if (!query.exec() || query.numRowsAffected() != 1) return false;
    }
    QSqlQuery query(connection.db);
    if (!query.exec(QStringLiteral("UPDATE project_results SET result_name='RK_IMPORT_' || result_id"))) return false;
    return connection.db.commit();
}
class Verification
{
public:
    int passed = 0, failed = 0;
    QJsonArray results;
    void check(bool success, const QString &name)
    {
        qInfo().noquote() << (success ? "PASS" : "FAIL") << name;
        success ? ++passed : ++failed;
        results.append(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("passed"), success}});
    }
};
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 5 || QString::fromLocal8Bit(argv[1]) != QStringLiteral("--data-dir") || QString::fromLocal8Bit(argv[3]) != QStringLiteral("--output-dir")) return 2;
    const QString root = QDir::cleanPath(QString::fromLocal8Bit(argv[2]));
    const QString output = QDir::cleanPath(QString::fromLocal8Bit(argv[4]));
    QFile testMarker(QDir(root).filePath(QStringLiteral(".sn100-original-rk-test-v1")));
    if (!QDir::isAbsolutePath(root) || !QDir::isAbsolutePath(output) || QFileInfo::exists(output) ||
        !testMarker.open(QIODevice::ReadOnly) || testMarker.readAll() != QByteArray("sn100-original-rk-test-v1\n") || !QDir().mkpath(output)) return 2;
    Verification verify;
    ProjectsStore::registerTypes();
    QThread worker;
    auto *store = new ProjectsStore;
    store->moveToThread(&worker);
    QObject::connect(&worker, &QThread::finished, store, &QObject::deleteLater);
    struct Receipt { bool success = false; qint64 projectId = 0; QString operation; };
    QMap<qint64, QList<Receipt>> receipts;
    ProjectRecords projects; ProjectUnits units;
    bool ready = false; QString activeRoot;
    qint64 sequence = 100000;
    QList<qint64> durations;
    QElapsedTimer total, heartbeat; total.start(); heartbeat.start();
    qint64 maximumGap = 0; QTimer timer; timer.setInterval(5);
    QObject::connect(&timer, &QTimer::timeout, &app, [&]() { maximumGap = qMax(maximumGap, heartbeat.restart()); }); timer.start();
    QObject::connect(store, &ProjectsStore::ready, &app, [&](const QString &) { ready = true; });
    QObject::connect(store, &ProjectsStore::sourceProfileChanged, &app, [&](const QString &path, bool original) { if (original) activeRoot = path; });
    QObject::connect(store, &ProjectsStore::succeeded, &app, [&](qint64 id, const QString &operation, qint64 projectId) { receipts[id].append({true, projectId, operation}); });
    QObject::connect(store, &ProjectsStore::error, &app, [&](qint64 id, const QString &operation, const QString &) { receipts[id].append({false, 0, operation}); });
    QObject::connect(store, &ProjectsStore::snapshotReady, &app, [&](qint64 id, const ProjectRecords &p, const ProjectUnits &u) { projects = p; units = u; receipts[id].append({true, 0, QStringLiteral("load")}); });
    worker.start();
    auto queue = [&](const std::function<void()> &operation) { QMetaObject::invokeMethod(store, operation, Qt::QueuedConnection); };
    auto call = [&](const std::function<void(qint64)> &operation, bool expected, const QString &label)
    {
        const qint64 id = ++sequence;
        QElapsedTimer elapsed; elapsed.start(); queue([=]() { operation(id); });
        const bool arrived = awaitResult([&]() { return !receipts.value(id).isEmpty(); });
        durations.append(elapsed.elapsed());
        verify.check(arrived && receipts.value(id).last().success == expected, label);
        return arrived && receipts.value(id).last().success;
    };
    auto load = [&]() { return call([=](qint64 id) { store->load(id); }, true, QStringLiteral("读取原工作副本")); };
    auto save = [&](const ProjectRecord &record, bool expected, const QString &label) { return call([=](qint64 id) { store->save(id, record); }, expected, label); };
    queue([=]() { store->initializeOriginal(root); });
    verify.check(awaitResult([&]() { return ready || !receipts.value(0).isEmpty(); }) && ready, QStringLiteral("原库在数据库工作线程初始化"));
    AccountSession session; session.name = QStringLiteral("RKOriginalConfigVerifier"); session.role = 4; session.authenticated = true; session.actionReady = true; session.source = AccountSessionSource::Simulated;
    queue([=]() { store->setSession(session); });
    load();
    verify.check(projects.size() == 7 && units.size() == 7, QStringLiteral("原七项目与七单位完整读取"));
    if (projects.size() != 7 || units.isEmpty()) { queue([=]() { store->close(); }); worker.quit(); worker.wait(); return 1; }
    bool parameterComplete = true, resultComplete = true, sourceComplete = true;
    for (const auto &project : projects)
    {
        parameterComplete &= !project.originalBaseValues.isEmpty() && project.originalAlgorithmValues.size() == 91;
        resultComplete &= project.results.size() == 5 && project.results.last().sourceResultId == 5;
        sourceComplete &= project.originalRoot == root && !project.originalFileName.isEmpty() && project.originalFingerprint.size() == 64;
    }
    verify.check(parameterComplete, QStringLiteral("七项目全部基础参数与91项算法参数保留"));
    verify.check(resultComplete, QStringLiteral("全部五行结果含第5行MDA保留"));
    verify.check(sourceComplete, QStringLiteral("项目来源、文件名和读取版本明确"));
    const ProjectRecords initial = projects;
    for (const auto &project : initial)
    {
        const QString path = projectPath(root, project); const QString before = hash(path);
        save(project, true, QStringLiteral("原项目不改动保存"));
        verify.check(hash(path) == before, QStringLiteral("不改动保存保持整份DB字节"));
    }
    load();
    ProjectRecord before = projects.first();
    const QString file = projectPath(root, before);
    const QString originalSchema = schemaDigest(file);
    const QStringList preservedTables = {QStringLiteral("AlgParam"), QStringLiteral("project_sample_rate"), QStringLiteral("project_reagent"), QStringLiteral("project_results"), QStringLiteral("project_mda"), QStringLiteral("project_quality")};
    QMap<QString, QString> beforeTables;
    for (const QString &table : preservedTables) beforeTables.insert(table, tableDigest(file, table));
    ProjectRecord edited = before; edited.fullName = QStringLiteral("RK匿名配置核对"); edited.algorithm.coefficientA = before.algorithm.coefficientA == 1.2345 ? 2.3456 : 1.2345;
    save(edited, true, QStringLiteral("正式保存项目全称与系数A"));
    load();
    ProjectRecord updated = findProject(projects, before.id);
    verify.check(updated.fullName == edited.fullName && updated.algorithm.coefficientA == edited.algorithm.coefficientA, QStringLiteral("保存后从原DB读回实际修改值"));
    bool allTablesPreserved = true;
    for (const QString &table : preservedTables) allTablesPreserved &= !beforeTables.value(table).isEmpty() && tableDigest(file, table) == beforeTables.value(table);
    verify.check(allTablesPreserved && schemaDigest(file) == originalSchema, QStringLiteral("仅编辑基础字段，其他六张表和原结构全保留"));
    auto oldBase = before.originalBaseValues; auto newBase = updated.originalBaseValues;
    for (const QString &field : {QStringLiteral("project_cn_name"), QStringLiteral("xishu_a"), QStringLiteral("change_time")}) { oldBase.remove(field); newBase.remove(field); }
    verify.check(oldBase == newBase, QStringLiteral("未编辑基础参数与系数B/C保持原文字"));
    save(before, false, QStringLiteral("旧版本项目草稿拒绝覆盖已保存数据"));
    ProjectRecord rawEdit = updated; rawEdit.originalAlgorithmValues.insert(QStringLiteral("Sort_startTime"), QStringLiteral("999"));
    save(rawEdit, false, QStringLiteral("只读算法参数不能借保存改写"));
    const QString failureHash = hash(file);
    const QFile::Permissions permissions = QFile::permissions(file);
    verify.check(QFile::setPermissions(file, QFileDevice::ReadOwner), QStringLiteral("设置测试副本只读故障"));
    edited = updated; edited.fullName = QStringLiteral("只读期间未保存");
    save(edited, false, QStringLiteral("只读原DB真实保存失败"));
    verify.check(QFile::setPermissions(file, permissions) && hash(file) == failureHash, QStringLiteral("只读失败后原字节未变化并恢复权限"));
    {
        AuditConnection lock;
        QSqlQuery held;
        bool locked = lock.open(file, true);
        if (locked) { held = QSqlQuery(lock.db); locked = held.exec(QStringLiteral("BEGIN IMMEDIATE")); }
        verify.check(locked, QStringLiteral("建立真实数据库写锁故障"));
        if (locked) save(edited, false, QStringLiteral("写锁冲突真实返回失败"));
        if (locked) held.exec(QStringLiteral("ROLLBACK"));
    }
    verify.check(hash(file) == failureHash, QStringLiteral("写锁失败没有部分保存"));
    save(edited, true, QStringLiteral("解除故障后同一输入可重新保存"));
    load(); updated = findProject(projects, before.id);
    const Rows resultsBefore = tableRows(file, QStringLiteral("project_results"));
    edited = updated; edited.results.first().lisCode = QStringLiteral("RK_ORIGINAL_RESULT");
    save(edited, true, QStringLiteral("结果设置写入原project_results")); load();
    updated = findProject(projects, before.id);
    Rows resultsAfter = tableRows(file, QStringLiteral("project_results"));
    Rows preservedBefore = resultsBefore;
    if (!preservedBefore.isEmpty() && !resultsAfter.isEmpty()) { preservedBefore.first().remove(QStringLiteral("lis_code")); resultsAfter.first().remove(QStringLiteral("lis_code")); }
    verify.check(!resultsBefore.isEmpty() && preservedBefore == resultsAfter && updated.results.first().lisCode == QStringLiteral("RK_ORIGINAL_RESULT"), QStringLiteral("结果只改指定字段，MDA及所有未知列保持"));
    edited = updated; for (auto &result : edited.results) result.mainResult = false;
    save(edited, false, QStringLiteral("结果主项无效时拒绝整份提交"));
    edited = updated; edited.results.last().lisCode = QStringLiteral("MDA_NOT_EDITABLE");
    save(edited, false, QStringLiteral("未开放MDA行不能被改写"));
    call([=](qint64 id) { store->copy(id, before.id, QStringLiteral("RK_EXTERNAL_COPY")); }, false, QStringLiteral("外部校准或批组关联项目复制明确拒绝"));
    call([=](qint64 id) { store->remove(id, before.id); }, false, QStringLiteral("固定或有原引用的项目不能删除"));
    call([=](qint64 id) { store->importOriginalFile(id, file); }, false, QStringLiteral("同名同编号原DB导入不覆盖"));
    ProjectUnit unit; unit.name = QStringLiteral("RK_ORIGINAL_UNIT"); unit.ratio = 1.25; unit.originalType = units.first().originalType;
    call([=](qint64 id) { store->addUnit(id, unit); }, true, QStringLiteral("原单位库新增保留小数倍率")); load();
    ProjectUnit firstUnit = findUnit(units, unit.name); ProjectUnit changedUnit = firstUnit; changedUnit.ratio = 2.5;
    verify.check(firstUnit.ratio == 1.25 && firstUnit.originalType == unit.originalType && firstUnit.originalFingerprint.size() == 64, QStringLiteral("新增单位倍率、分类和读取版本一致"));
    call([=](qint64 id) { store->updateUnit(id, firstUnit.name, changedUnit); }, true, QStringLiteral("修改原单位小数倍率")); load();
    const ProjectUnit currentUnit = findUnit(units, unit.name);
    verify.check(currentUnit.ratio == 2.5 && currentUnit.originalType == firstUnit.originalType, QStringLiteral("单位修改读回未取整或改分类"));
    call([=](qint64 id) { store->updateUnit(id, firstUnit.name, firstUnit); }, false, QStringLiteral("旧单位版本拒绝覆盖外部变化"));
    call([=](qint64 id) { store->removeUnit(id, currentUnit.name); }, false, QStringLiteral("原单位无版本删除拒绝执行"));
    call([=](qint64 id) { store->removeOriginalUnit(id, currentUnit.name, firstUnit.originalFingerprint); }, false, QStringLiteral("旧单位版本拒绝删除"));
    call([=](qint64 id) { store->removeOriginalUnit(id, currentUnit.name, currentUnit.originalFingerprint); }, true, QStringLiteral("按当前版本删除未使用单位")); load();
    verify.check(findUnit(units, unit.name).name.isEmpty(), QStringLiteral("单位删除实际读回不存在"));
    const QString exportRoot = output + QStringLiteral("/export-all");
    call([=](qint64 id) { store->exportOriginalBundle(id, 0, exportRoot); }, true, QStringLiteral("导出全部原项目配置"));
    bool treeCorrect = QFileInfo::exists(exportRoot + QStringLiteral("/db/systemInfo/option_Unit.db")) && QFileInfo::exists(exportRoot + QStringLiteral("/db/userSet/reagent_Information.db")) && !QFileInfo::exists(exportRoot + QStringLiteral("/catalog.db"));
    for (const auto &project : projects) treeCorrect &= QFileInfo::exists(projectPath(exportRoot, project));
    verify.check(treeCorrect && QDir(exportRoot + QStringLiteral("/project")).entryList({QStringLiteral("*.db")}, QDir::Files).size() == 7, QStringLiteral("导出严格保持原目录分类与原文件名"));
    const QString exportOne = output + QStringLiteral("/export-one");
    call([=](qint64 id) { store->exportOriginalBundle(id, before.id, exportOne); }, true, QStringLiteral("导出当前原项目配置"));
    verify.check(QDir(exportOne + QStringLiteral("/project")).entryList({QStringLiteral("*.db")}, QDir::Files).size() == 1, QStringLiteral("单项目导出只有指定项目和必要配置"));
    const ProjectRecord rootADraft = findProject(projects, before.id); const ProjectUnit rootAUnit = units.first();
    const QString identicalRoot = output + QStringLiteral("/identical-root");
    verify.check(copyConfiguration(root, identicalRoot), QStringLiteral("准备同字节不同根的版本冲突副本"));
    call([=](qint64 id) { store->openOriginalDirectory(id, identicalRoot); }, true, QStringLiteral("切换另一个原工作目录")); load();
    save(rootADraft, false, QStringLiteral("原目录旧项目草稿不得写入新目录"));
    call([=](qint64 id) { store->updateUnit(id, rootAUnit.name, rootAUnit); }, false, QStringLiteral("同字节不同根旧单位草稿也拒绝"));
    call([=](qint64 id) { store->openOriginalDirectory(id, output + QStringLiteral("/not-present")); }, false, QStringLiteral("无效目录切换失败")); load();
    verify.check(activeRoot == identicalRoot && projects.size() == 7, QStringLiteral("切换失败保留原工作目录和数据"));
    call([=](qint64 id) { store->openOriginalDirectory(id, exportRoot); }, true, QStringLiteral("导出的原目录可重新读取")); load();
    edited = findProject(projects, before.id); edited.fullName = QStringLiteral("导出库重读保存");
    save(edited, true, QStringLiteral("导出库可继续保存基础参数")); load();
    verify.check(findProject(projects, before.id).fullName == edited.fullName, QStringLiteral("导出配置读写闭环成立"));
    call([=](qint64 id) { store->openOriginalDirectory(id, root); }, true, QStringLiteral("切回完整原工作副本"));
    // 保留刚刚完成的目录写请求号；随后load有独立读请求号，不能拿它伪装重复写。
    const qint64 duplicateId = sequence;
    load();
    const int receiptCount = receipts.value(duplicateId).size();
    queue([=]() { store->openOriginalDirectory(duplicateId, exportRoot); });
    verify.check(awaitResult([&]() { return receipts.value(duplicateId).size() > receiptCount; }) && !receipts.value(duplicateId).last().success && activeRoot == root, QStringLiteral("重复目录写请求不再次执行"));
    const QString fixture = output + QStringLiteral("/8999 RK_ORIGINAL_IMPORT.db");
    verify.check(prepareImportFixture(projectPath(exportRoot, before), fixture), QStringLiteral("准备匿名独立身份原格式导入夹具"));
    call([=](qint64 id) { store->importOriginalFile(id, fixture); }, true, QStringLiteral("正式导入原格式完整七表项目DB")); load();
    ProjectRecord imported = findProject(projects, 8999);
    bool importedExact = imported.id == 8999;
    const QString importedFile = projectPath(root, imported);
    for (const QString &table : preservedTables + QStringList{QStringLiteral("project_base")}) importedExact &= !tableDigest(fixture, table).isEmpty() && tableDigest(fixture, table) == tableDigest(importedFile, table);
    verify.check(importedExact && imported.originalFileName == QFileInfo(fixture).fileName(), QStringLiteral("导入完整七表值与原选入文件名一致"));
    qint64 copyId = 0;
    const qint64 copyRequest = sequence + 1;
    call([=](qint64 id) { store->copy(id, imported.id, QStringLiteral("RK_ORIGINAL_COPY")); }, true, QStringLiteral("无外部关联原项目正式复制"));
    if (!receipts.value(copyRequest).isEmpty()) copyId = receipts.value(copyRequest).last().projectId;
    load();
    const ProjectRecord copied = findProject(projects, copyId);
    verify.check(copied.id > 0 && copied.name == QStringLiteral("RK_ORIGINAL_COPY") && copied.results.size() == 5 && copied.originalAlgorithmValues == imported.originalAlgorithmValues, QStringLiteral("复制项目身份更新且五结果与全部算法保留"));
    const QString copiedFile = projectPath(root, copied);
    bool copiedPreserved = true;
    for (const QString &table : {QStringLiteral("AlgParam"), QStringLiteral("project_sample_rate"), QStringLiteral("project_reagent"), QStringLiteral("project_mda"), QStringLiteral("project_quality")}) copiedPreserved &= tableDigest(copiedFile, table) == tableDigest(importedFile, table);
    verify.check(copied.id > 0 && copiedPreserved, QStringLiteral("复制保留动作、MDA和质控等原表完整数据"));
    call([=](qint64 id) { store->remove(id, copyId); }, true, QStringLiteral("删除无外部关联的复制项目"));
    call([=](qint64 id) { store->remove(id, imported.id); }, true, QStringLiteral("删除匿名导入项目")); load();
    verify.check(projects.size() == 7 && !QFileInfo::exists(copiedFile) && !QFileInfo::exists(importedFile), QStringLiteral("导入复制删除闭环后仅保留原七项目"));
    bool integrity = true; for (const auto &project : projects) integrity &= quickCheck(projectPath(root, project));
    integrity &= quickCheck(root + QStringLiteral("/db/systemInfo/option_Unit.db"));
    verify.check(integrity, QStringLiteral("全部改动后原项目与单位库完整性通过"));
    bool closed = false; QObject::connect(store, &ProjectsStore::closed, &app, [&]() { closed = true; }); queue([=]() { store->close(); });
    verify.check(awaitResult([&]() { return closed; }), QStringLiteral("数据库线程按顺序关闭")); worker.quit(); worker.wait();
    timer.stop();
    std::sort(durations.begin(), durations.end());
    const qint64 p95 = durations.isEmpty() ? 0 : durations[qMin(durations.size() - 1, int(durations.size() * .95))];
    const QJsonObject report{{QStringLiteral("passed"), verify.passed}, {QStringLiteral("failed"), verify.failed}, {QStringLiteral("elapsedMs"), total.elapsed()},
        {QStringLiteral("requestCount"), durations.size()}, {QStringLiteral("requestP95Ms"), p95}, {QStringLiteral("maximumMainEventLoopGapMs"), maximumGap},
        {QStringLiteral("scope"), QStringLiteral("RK原配置库读写；不连接仪器；专用测试副本；不输出患者、账户或原参数值。只读SQL用于逐表核对，SQL写入仅匿名夹具准备和故障锁。")},
        {QStringLiteral("results"), verify.results}};
    QFile reportFile(output + QStringLiteral("/verification.json"));
    if (!reportFile.open(QIODevice::WriteOnly)) return 2;
    reportFile.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
    qInfo() << "ORIGINAL_STORE_RESULT" << verify.passed << verify.failed << "p95Ms" << p95 << "maximumMainEventLoopGapMs" << maximumGap;
    return verify.failed == 0 ? 0 : 1;
}
