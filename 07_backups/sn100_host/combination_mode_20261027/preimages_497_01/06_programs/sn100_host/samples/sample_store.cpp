#include "sample_store.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QTimer>
#include <QUuid>

namespace
{
QString problem(const QSqlQuery &query)
{
    return query.lastError().text();
}
bool execute(QSqlDatabase database, const QString &sql, const QVariantList &values, QString *message)
{
    QSqlQuery query(database);
    if (!query.prepare(sql))
    {
        *message = problem(query);
        return false;
    }
    for (const QVariant &value : values)
    {
        query.addBindValue(value);
    }
    if (!query.exec())
    {
        *message = problem(query);
        return false;
    }
    return true;
}
QString stateProblem(const SampleTask &task)
{
    if (task.repeats < 0 || task.repeats > 99 ||
        (!task.states.isEmpty() && task.states.size() != task.repeats))
    {
        return QStringLiteral("逐次测试状态数量必须匹配0至99次重复，原数据保留。");
    }
    for (SampleTaskState state : task.states)
    {
        if (static_cast<int>(state) < 0 || static_cast<int>(state) > 4)
        {
            return QStringLiteral("未知的本地测试状态，原数据保留。");
        }
    }
    return QString();
}
bool sameHole(const SampleHole &left, const SampleHole &right)
{
    if (left.sampleId != right.sampleId || left.container != right.container ||
        left.revise != right.revise || left.tasks.size() != right.tasks.size())
    {
        return false;
    }
    for (const SampleTask &task : left.tasks)
    {
        bool found = false;
        for (const SampleTask &other : right.tasks)
        {
            if (task.projectId == other.projectId)
            {
                found = task.rate == other.rate && task.repeats == other.repeats &&
                        task.prolonged == other.prolonged && sampleTaskStates(task) == sampleTaskStates(other);
                break;
            }
        }
        if (!found)
        {
            return false;
        }
    }
    return true;
}
bool rackHasLockedHole(const SampleRack &rack)
{
    for (const SampleHole &hole : rack.holes)
    {
        if (!hole.revise || sampleHoleState(sampleHoleTaskStates(hole)) == SampleHoleState::Testing)
        {
            return true;
        }
    }
    return false;
}
QString stateSchemaProblem(QSqlDatabase database)
{
    QSqlQuery query(database);
    if (!query.exec("SELECT value FROM catalog_metadata WHERE key='sample_task_state_schema'"))
    {
        return problem(query);
    }
    const bool marked = query.next();
    if (marked && query.value(0).toString() != "sn100-sample-task-state-v1")
    {
        return QStringLiteral("样本逐次状态版本不支持，保留原数据。");
    }
    query.finish();
    if (!query.exec("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name IN "
                    "('sample_local_hole_state','sample_local_task_state')") || !query.next())
    {
        return problem(query);
    }
    if (query.value(0).toInt() != (marked ? 2 : 0))
    {
        return QStringLiteral("逐次状态表与版本标记不一致，保留原数据。");
    }
    return QString();
}
} // namespace
SampleStore::SampleStore(QObject *parent) : QObject(parent)
{
    registerTypes();
    setupQualityProtocol();
}
SampleStore::~SampleStore()
{
    release();
}
void SampleStore::registerTypes()
{
    qRegisterMetaType<AccountSession>();
    qRegisterMetaType<SampleTask>();
    qRegisterMetaType<SampleHole>();
    qRegisterMetaType<SampleRack>();
    qRegisterMetaType<SampleCombination>();
    qRegisterMetaType<SampleSnapshot>();
}
bool SampleStore::canEdit(const AccountSession &session)
{
    // Original sample entry has no engineer-only gate. The local preset writer
    // requires an explicit authenticated offline identity, separately recorded.
    // Original presets may be prepared while other instrument work is running.
    // actionReady is therefore not a gate for these local-only edits.
    return session.authenticated && session.role >= 1 && session.role <= 5;
}
bool SampleStore::check(qint64 request, const QString &operation, bool write)
{
    if (QThread::currentThread() != thread())
    {
        emit error(request, operation, QStringLiteral("样本存储调用线程错误。"));
        return false;
    }
    if (!ready_)
    {
        emit error(request, operation, QStringLiteral("样本库未就绪。"));
        return false;
    }
    if (write && !pendingQuality_.change.changeToken.isEmpty())
    {
        emit error(request, operation, QStringLiteral("样本引用事务尚未结束，请稍后。"));
        return false;
    }
    if (write && !canEdit(session_))
    {
        emit error(request, operation, QStringLiteral("本地预设编辑须显式已认证身份。"));
        return false;
    }
    return true;
}
void SampleStore::initialize(QString directory)
{
    if (QThread::currentThread() != thread())
    {
        emit error(0, "initialize", "样本存储线程错误。");
        return;
    }
    if (!pendingQuality_.change.changeToken.isEmpty())
    { emit error(0,"initialize",QStringLiteral("引用事务处理中，不能重新初始化。")); return; }
    release();
    auto fail = [this](QString message)
    {
        emit error(0, QStringLiteral("initialize"), message);
        release();
    };
    directory = QDir::cleanPath(directory);
    if (!QDir::isAbsolutePath(directory) || directory.contains("sn100_host_reference", Qt::CaseInsensitive))
    {
        fail(QStringLiteral("仅接受项目模块已初始化的隔离绝对目录。"));
        return;
    }
    for (QFileInfo current(directory); !current.filePath().isEmpty(); current.setFile(current.absolutePath()))
    {
        if (current.isSymLink())
        {
            fail(QStringLiteral("隔离目录不能经过符号链接。"));
            return;
        }
        if (current.absoluteFilePath() == current.absolutePath())
        {
            break;
        }
    }
    QDir dir(directory);
    const QFileInfo marker(dir.filePath(".sn100-catalog-offline-v1"));
    const QFileInfo database(dir.filePath("catalog.db"));
    if (!marker.isFile() || marker.isSymLink() || !database.isFile() || database.isSymLink())
    {
        fail(QStringLiteral("缺少项目隔离标记/数据库，或路径是链接。"));
        return;
    }
    QFile file(marker.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly) || file.read(128).trimmed() != "sn100-catalog-offline-v1")
    {
        fail(QStringLiteral("项目隔离标记内容不匹配。"));
        return;
    }
    connection_ = QStringLiteral("samples-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    database_ = QSqlDatabase::addDatabase("QSQLITE", connection_);
    database_.setDatabaseName(database.absoluteFilePath());
    database_.setConnectOptions("QSQLITE_BUSY_TIMEOUT=800");
    if (!database_.open())
    {
        fail(database_.lastError().text());
        return;
    }
    QString message;
    {
        QSqlQuery query(database_);
        if (!query.exec("SELECT value FROM catalog_metadata WHERE key='schema'") || !query.next() ||
            query.value(0).toString() != "sn100-catalog-offline-v1")
        {
            fail(QStringLiteral("共享库结构不匹配，未迁移未知数据。"));
            return;
        }
        query.finish();
        if (!query.exec("PRAGMA foreign_keys=ON") || !query.exec("PRAGMA foreign_keys") || !query.next() ||
            query.value(0).toInt() != 1)
        {
            fail(QStringLiteral("无法启用样本引用保护。"));
            return;
        }
        query.finish();
        if (!query.exec("SELECT value FROM catalog_metadata WHERE key='sample_schema'"))
        {
            fail(problem(query));
            return;
        }
        if (query.next())
        {
            if (query.value(0).toString() != "sn100-sample-local-v1")
            {
                fail(QStringLiteral("样本预设结构版本不支持。"));
                return;
            }
        }
        else
        {
            query.finish();
            if (!query.exec("SELECT COUNT(*) FROM sqlite_master WHERE name "
                            "IN('sample_local_rack','sample_local_hole','sample_local_task','sample_"
                            "combination','sample_combination_member','sample_emergency_unique')") ||
                !query.next() || query.value(0).toInt() != 0)
            {
                fail(QStringLiteral("样本表已存在但缺少来源版本标记，保留原数据。"));
                return;
            }
        }
    }
    if (!execute(database_, "BEGIN IMMEDIATE", {}, &message))
    {
        fail(message);
        return;
    }
    // 同一写事务内核版本；未知/缺损的扩展不被CREATE IF NOT EXISTS悄悄重建。
    message = stateSchemaProblem(database_);
    if (!message.isEmpty())
    {
        database_.rollback();
        fail(message);
        return;
    }
    const QStringList schema = {
        "CREATE TABLE IF NOT EXISTS sample_local_rack(id INTEGER PRIMARY KEY AUTOINCREMENT,name TEXT NOT "
        "NULL UNIQUE,emergency INTEGER NOT NULL CHECK(emergency IN(0,1)))",
        "CREATE UNIQUE INDEX IF NOT EXISTS sample_emergency_unique ON sample_local_rack(emergency) WHERE "
        "emergency=1",
        "CREATE TABLE IF NOT EXISTS sample_local_hole(rack_id INTEGER NOT NULL REFERENCES "
        "sample_local_rack(id) ON DELETE CASCADE,number INTEGER NOT NULL,sample_id TEXT NOT NULL,container "
        "INTEGER NOT NULL CHECK(container IN(0,1)),PRIMARY KEY(rack_id,number))",
        "CREATE TABLE IF NOT EXISTS sample_local_task(rack_id INTEGER NOT NULL,hole_number INTEGER NOT "
        "NULL,project_id INTEGER NOT NULL REFERENCES project_base(id) ON DELETE RESTRICT,rate TEXT NOT "
        "NULL,repeats INTEGER NOT NULL CHECK(repeats BETWEEN 0 AND 99),prolonged INTEGER NOT NULL "
        "CHECK(prolonged IN(0,1)),PRIMARY KEY(rack_id,hole_number,project_id),FOREIGN "
        "KEY(rack_id,hole_number) REFERENCES sample_local_hole(rack_id,number) ON DELETE CASCADE)",
        "CREATE TABLE IF NOT EXISTS sample_local_hole_state(rack_id INTEGER NOT NULL,hole_number INTEGER "
        "NOT NULL,revise INTEGER NOT NULL CHECK(revise IN(0,1)),PRIMARY KEY(rack_id,hole_number),FOREIGN "
        "KEY(rack_id,hole_number) REFERENCES sample_local_hole(rack_id,number) ON DELETE CASCADE)",
        "CREATE TABLE IF NOT EXISTS sample_local_task_state(rack_id INTEGER NOT NULL,hole_number INTEGER "
        "NOT NULL,project_id INTEGER NOT NULL,states TEXT NOT NULL,PRIMARY KEY(rack_id,hole_number,project_id),"
        "FOREIGN KEY(rack_id,hole_number,project_id) REFERENCES sample_local_task(rack_id,hole_number,project_id) "
        "ON DELETE CASCADE)",
        "CREATE TABLE IF NOT EXISTS sample_combination(id INTEGER PRIMARY KEY AUTOINCREMENT,name TEXT NOT "
        "NULL,project_contain TEXT NOT NULL)",
        "CREATE TABLE IF NOT EXISTS sample_combination_member(combination_id INTEGER NOT NULL REFERENCES "
        "sample_combination(id) ON DELETE CASCADE,position INTEGER NOT NULL,project_id INTEGER NOT NULL "
        "REFERENCES project_base(id) ON DELETE RESTRICT,PRIMARY KEY(combination_id,position))",
        "INSERT OR IGNORE INTO sample_local_rack(name,emergency) VALUES('Emergency',1)",
        "INSERT OR IGNORE INTO sample_local_hole VALUES((SELECT id FROM sample_local_rack WHERE "
        "emergency=1),1,'',1)",
        "INSERT OR IGNORE INTO sample_local_hole VALUES((SELECT id FROM sample_local_rack WHERE "
        "emergency=1),2,'',1)",
        "INSERT OR IGNORE INTO sample_local_hole VALUES((SELECT id FROM sample_local_rack WHERE "
        "emergency=1),3,'',1)",
        "INSERT OR IGNORE INTO catalog_metadata VALUES('sample_schema','sn100-sample-local-v1')",
        "INSERT OR IGNORE INTO catalog_metadata VALUES('sample_task_state_schema','sn100-sample-task-state-v1')",
        "INSERT OR IGNORE INTO catalog_metadata VALUES('sample_layout_source','source-default ordinary=8 "
        "emergency=3; instrument unverified')"};
    bool ok = true;
    for (const QString &sql : schema)
    {
        if (!(ok = execute(database_, sql, {}, &message)))
        {
            break;
        }
    }
    if (ok) ok = initializeQualitySchema(&message);
    if (ok)
    {
        ok = database_.commit();
    }
    if (!ok)
    {
        database_.rollback();
        fail(message.isEmpty() ? database_.lastError().text() : message);
        return;
    }
    ready_ = true;
    emit ready(database.absoluteFilePath());
    emit qualityOwnerBindingRequired(qualityOwnerId_,database.canonicalFilePath());
}
void SampleStore::setSession(AccountSession session)
{
    if (QThread::currentThread() == thread())
    {
        session_ = session;
    }
}
bool SampleStore::read(SampleSnapshot *snapshot, QString *message, qint64 onlyRack)
{
    *message = stateSchemaProblem(database_);
    if (!message->isEmpty())
    {
        return false;
    }
    QSqlQuery racks(database_);
    racks.prepare(onlyRack ? "SELECT id,name,emergency FROM sample_local_rack WHERE id=?"
                           : "SELECT id,name,emergency FROM sample_local_rack ORDER BY emergency DESC,id");
    if (onlyRack)
    {
        racks.addBindValue(onlyRack);
    }
    if (!racks.exec())
    {
        *message = problem(racks);
        return false;
    }
    while (racks.next())
    {
        SampleRack rack;
        rack.id = racks.value(0).toLongLong();
        rack.name = racks.value(1).toString();
        rack.emergency = racks.value(2).toBool();
        QSqlQuery holes(database_);
        holes.prepare(
            "SELECT number,sample_id,container FROM sample_local_hole WHERE rack_id=? ORDER BY number");
        holes.addBindValue(rack.id);
        if (!holes.exec())
        {
            *message = problem(holes);
            return false;
        }
        while (holes.next())
        {
            SampleHole hole;
            hole.number = holes.value(0).toInt();
            hole.sampleId = holes.value(1).toString();
            hole.container = holes.value(2).toInt();
            QSqlQuery savedState(database_);
            savedState.prepare("SELECT revise FROM sample_local_hole_state WHERE rack_id=? AND hole_number=?");
            savedState.addBindValue(rack.id);
            savedState.addBindValue(hole.number);
            if (!savedState.exec())
            {
                *message = problem(savedState);
                return false;
            }
            if (savedState.next())
            {
                bool valid = false;
                const int revise = savedState.value(0).toInt(&valid);
                if (!valid || (revise != 0 && revise != 1))
                {
                    *message = QStringLiteral("孔位可修改标记损坏，保留原数据。");
                    return false;
                }
                hole.revise = revise == 1;
            }
            savedState.finish();
            QSqlQuery tasks(database_);
            tasks.prepare("SELECT project_id,rate,repeats,prolonged FROM sample_local_task WHERE rack_id=? "
                          "AND hole_number=? ORDER BY project_id");
            tasks.addBindValue(rack.id);
            tasks.addBindValue(hole.number);
            if (!tasks.exec())
            {
                *message = problem(tasks);
                return false;
            }
            while (tasks.next())
            {
                SampleTask task{tasks.value(0).toLongLong(), tasks.value(1).toString(),
                                tasks.value(2).toInt(), tasks.value(3).toBool()};
                savedState.prepare("SELECT states FROM sample_local_task_state WHERE rack_id=? "
                                   "AND hole_number=? AND project_id=?");
                savedState.addBindValue(rack.id);
                savedState.addBindValue(hole.number);
                savedState.addBindValue(task.projectId);
                if (!savedState.exec())
                {
                    *message = problem(savedState);
                    return false;
                }
                if (savedState.next())
                {
                    const QByteArray raw = savedState.value(0).toString().toUtf8();
                    QJsonParseError error{};
                    const QJsonDocument document = raw.size() <= 1024
                                                       ? QJsonDocument::fromJson(raw, &error) : QJsonDocument();
                    if (error.error != QJsonParseError::NoError || !document.isArray() ||
                        document.array().size() != task.repeats)
                    {
                        *message = QStringLiteral("逐次测试状态内容/数量损坏，保留原数据。");
                        return false;
                    }
                    for (const QJsonValue &value : document.array())
                    {
                        const int state = value.toInt(-1);
                        if (!value.isDouble() || state < 0 || state > 4 || value.toDouble() != state)
                        {
                            *message = QStringLiteral("逐次测试状态包含未知值，保留原数据。");
                            return false;
                        }
                        task.states.append(static_cast<SampleTaskState>(state));
                    }
                }
                savedState.finish();
                *message = stateProblem(task);
                if (!message->isEmpty())
                {
                    return false;
                }
                hole.tasks.append(task);
            }
            if (!readQualityHole(rack.id,&hole,message)) return false;
            rack.holes.append(hole);
        }
        snapshot->racks.append(rack);
    }
    if (onlyRack)
    {
        return true;
    }
    if (!racks.exec("SELECT id,name FROM sample_combination ORDER BY id"))
    {
        *message = problem(racks);
        return false;
    }
    while (racks.next())
    {
        SampleCombination combination;
        combination.id = racks.value(0).toLongLong();
        combination.name = racks.value(1).toString();
        QSqlQuery members(database_);
        members.prepare(
            "SELECT project_id FROM sample_combination_member WHERE combination_id=? ORDER BY position");
        members.addBindValue(combination.id);
        if (!members.exec())
        {
            *message = problem(members);
            return false;
        }
        while (members.next())
        {
            combination.orderedProjectIds.append(members.value(0).toLongLong());
        }
        snapshot->combinations.append(combination);
    }
    return true;
}
void SampleStore::load(qint64 request)
{
    if (!check(request, "load"))
    {
        return;
    }
    SampleSnapshot snapshot;
    QString message;
    if (!database_.transaction())
    {
        emit error(request, "load", database_.lastError().text());
        return;
    }
    if (!read(&snapshot, &message) || !database_.commit())
    {
        database_.rollback();
        emit error(request, "load", message.isEmpty() ? database_.lastError().text() : message);
        return;
    }
    emit snapshotReady(request, snapshot);
}
QString SampleStore::validateRack(const SampleRack &rack)
{
    const QString schema = stateSchemaProblem(database_);
    if (!schema.isEmpty())
    {
        return schema;
    }
    const int holes = rack.emergency ? 3 : 8; // Source defaults, explicitly not instrument discovery.
    static const QRegularExpression rackName("^S(0[1-9]|[1-8][0-9]|9[0-8])$");
    if ((rack.emergency && rack.name != "Emergency") ||
        (!rack.emergency && !rackName.match(rack.name).hasMatch()))
    {
        return QStringLiteral("普通预设架号须为S01至S98；急诊架名不可更改。");
    }
    if (rack.holes.size() != holes)
    {
        return QStringLiteral("孔数须匹配本地源码默认普通8/急诊3；真实型号待核。");
    }
    QSet<int> numbers;
    for (const SampleHole &hole : rack.holes)
    {
        if (hole.number < 1 || hole.number > holes || numbers.contains(hole.number) || hole.container < 0 ||
            hole.container > 1)
        {
            return QStringLiteral("孔号重复/越界或容器类型错误。");
        }
        numbers.insert(hole.number);
        // The original detail dialog accepts at most four letters/digits. Empty
        // remains an automatically numbered preset; no report counter is read.
        static const QRegularExpression sampleId("^[A-Za-z0-9]{0,4}$");
        const QString identity = qualityHoleProblem(hole);
        if (!identity.isEmpty()) return identity;
        if (hole.sampleType == 0 && !sampleId.match(hole.sampleId).hasMatch())
        {
            return QStringLiteral("样本号须为空或最多4位字母数字。");
        }
        QSet<qint64> projects;
        if (hole.tasks.size() > 128)
        {
            return QStringLiteral("单孔项目数超过本地预算。");
        }
        for (const SampleTask &task : hole.tasks)
        {
            const QString states = stateProblem(task);
            if (!states.isEmpty())
            {
                return states;
            }
            if (task.projectId <= 0 || projects.contains(task.projectId) || task.repeats < 0 ||
                task.repeats > 99)
            {
                return QStringLiteral("项目重复或次数不在0至99内。");
            }
            projects.insert(task.projectId);
            QSqlQuery project(database_);
            project.prepare("SELECT is_used FROM project_base WHERE id=?");
            project.addBindValue(task.projectId);
            if (!project.exec())
            {
                return problem(project);
            }
            if (!project.next() || !project.value(0).toBool())
            {
                return QStringLiteral("引用项目已不存在或未启用，请重新选择。");
            }
            project.finish();
            project.prepare(
                "SELECT 1 FROM project_sample_rate WHERE project_id=? AND sample_rate=? AND sample_option=1");
            project.addBindValue(task.projectId);
            project.addBindValue(task.rate);
            if (!project.exec())
            {
                return problem(project);
            }
            if (!project.next())
            {
                return QStringLiteral("项目缺少所选有效稀释标签；未配置参数不能视为可执行任务。");
            }
        }
    }
    return QString();
}
bool SampleStore::finish(qint64 request, const QString &operation, qint64 id, bool ok, QString message)
{
    if (ok && !database_.commit())
    {
        ok = false;
        message = database_.lastError().text();
    }
    if (!ok)
    {
        database_.rollback();
        emit error(request, operation,
                   message.contains("FOREIGN KEY") ? QStringLiteral("引用资料已变化或仍被引用，请刷新核对。")
                                                   : message);
        return false;
    }
    emit succeeded(request, operation, id);
    return true;
}
void SampleStore::saveCombination(qint64 request, SampleCombination combination)
{
    if (!check(request, "saveCombination", true))
    {
        return;
    }
    if (combination.name.trimmed().isEmpty() || combination.name.size() > 64 ||
        combination.name.contains(QChar::Null) || combination.orderedProjectIds.size() > 512)
    {
        emit error(request, "saveCombination", "组合名不能为空/超过64字，单组最多512项。");
        return;
    }
    if (!database_.transaction())
    {
        emit error(request, "saveCombination", database_.lastError().text());
        return;
    }
    QStringList names;
    QString message;
    QHash<qint64, int> counts;
    for (qint64 id : combination.orderedProjectIds)
    {
        if (++counts[id] > 99)
        {
            finish(request, "saveCombination", combination.id, false, "组合单项目不能超过99次。");
            return;
        }
        QSqlQuery query(database_);
        query.prepare("SELECT project_en_name FROM project_base WHERE id=? AND is_used=1");
        query.addBindValue(id);
        if (!query.exec() || !query.next())
        {
            finish(request, "saveCombination", combination.id, false, "组合引用项目已不存在或未启用。");
            return;
        }
        names.append(query.value(0).toString());
    }
    QSqlQuery query(database_);
    query.prepare(combination.id ? "UPDATE sample_combination SET name=?,project_contain=? WHERE id=?"
                                 : "INSERT INTO sample_combination(name,project_contain) VALUES(?,?)");
    query.addBindValue(combination.name);
    query.addBindValue(names.isEmpty() ? QStringLiteral("") : names.join(';'));
    if (combination.id)
    {
        query.addBindValue(combination.id);
    }
    if (!query.exec() || query.numRowsAffected() != 1)
    {
        finish(request, "saveCombination", combination.id, false,
               problem(query).isEmpty() ? QStringLiteral("组合已不存在。") : problem(query));
        return;
    }
    if (!combination.id)
    {
        combination.id = query.lastInsertId().toLongLong();
    }
    bool ok = execute(database_, "DELETE FROM sample_combination_member WHERE combination_id=?",
                      {combination.id}, &message);
    for (int i = 0; ok && i < combination.orderedProjectIds.size(); ++i)
    {
        ok = execute(database_, "INSERT INTO sample_combination_member VALUES(?,?,?)",
                     {combination.id, i, combination.orderedProjectIds[i]}, &message);
    }
    finish(request, "saveCombination", combination.id, ok, message);
}
void SampleStore::removeCombination(qint64 request, qint64 id)
{
    if (!check(request, "removeCombination", true))
    {
        return;
    }
    if (!database_.transaction())
    {
        emit error(request, "removeCombination", database_.lastError().text());
        return;
    }
    QSqlQuery query(database_);
    query.prepare("DELETE FROM sample_combination WHERE id=?");
    query.addBindValue(id);
    const bool ok = query.exec() && query.numRowsAffected() == 1;
    finish(request, "removeCombination", id, ok,
           problem(query).isEmpty() ? QStringLiteral("组合已不存在。") : problem(query));
}
void SampleStore::release()
{
    if (qualityReplyTimer_) qualityReplyTimer_->stop();
    pendingQuality_ = PendingQualityChange();
    qualityOwnerId_.clear(); qualityTargetId_.clear(); qualityBound_ = false; closeAfterQuality_ = false;
    ownQualityChanges_.clear();
    ready_ = false;
    if (database_.isValid())
    {
        database_.close();
    }
    database_ = QSqlDatabase();
    if (!connection_.isEmpty())
    {
        QSqlDatabase::removeDatabase(connection_);
    }
    connection_.clear();
    emit qualityProtectionReady(false);
}
void SampleStore::close()
{
    if (QThread::currentThread() != thread())
    {
        return;
    }
    if (!pendingQuality_.change.changeToken.isEmpty())
    {
        closeAfterQuality_ = true; // 有界等待真实回执；超时保留持久预约，供重启核对。
        return;
    }
    release();
    session_ = AccountSession();
    emit closed();
}
