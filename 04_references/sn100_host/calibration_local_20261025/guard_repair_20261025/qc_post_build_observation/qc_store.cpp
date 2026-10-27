#include "qc_store.h"
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>
#include <cmath>

namespace
{
const QString marker = QStringLiteral(".sn100-qc-offline-v1");
const QString schema = QStringLiteral("sn100-qc-offline-v1");
bool validText(const QString &value, int length)
{
    // 原调用长度16/32；保留中文文字，排除控制字符和首尾空白。
    if (value.isEmpty() || value.size() > length || value != value.trimmed())
        return false;
    for (const QChar c : value)
        if (c.isNull() || c.category() == QChar::Other_Control || c.category() == QChar::Other_Format)
            return false;
    return true;
}
bool validName(const QString &value, int length)
{
    static const QRegularExpression expression(QStringLiteral("^[\\p{L}\\p{N}][\\p{L}\\p{N}_() .%+\\-]*$"));
    return validText(value, length) && expression.match(value).hasMatch();
}
bool safeDirectory(const QString &path)
{
    if (!QDir::isAbsolutePath(path) || path.contains(QStringLiteral("sn100_host_reference"), Qt::CaseInsensitive))
        return false;
    for (QFileInfo current(path); !current.filePath().isEmpty(); current.setFile(current.absolutePath()))
    {
        if (current.isSymLink())
            return false;
        if (current.absoluteFilePath() == current.absolutePath())
            break;
    }
    return true;
}
bool run(QSqlQuery *query, const QString &sql, const QVariantList &values, QString *message)
{
    if (!query->prepare(sql))
    {
        *message = query->lastError().text();
        return false;
    }
    for (const QVariant &value : values)
        query->addBindValue(value);
    if (!query->exec())
    {
        *message = query->lastError().text();
        return false;
    }
    return true;
}
const QString ruleColumns = QStringLiteral("upper_limit=?,lower_limit=?,upper_bound=?,lower_bound=?,sd_value=?,target=?,r1_2s=?,r1_3s=?,r2_2s=?,r4_1s=?,r5_5s=?,rR4s=?,r7T=?,r10X=?,limit_control=?,used=?");
QVariantList ruleValues(const QcRule &rule)
{
    QVariantList values{rule.upperLimit, rule.lowerLimit, rule.upperBound, rule.lowerBound, rule.sdValue, rule.target};
    for (int strategy : rule.strategies)
        values.append(strategy);
    values.append(rule.used);
    return values;
}
QString validateCatalog(const QcProjectRefs &projects)
{
    QSet<qint64> ids;
    QSet<QString> names;
    for (const QcProjectRef &project : projects)
    {
        if (project.id <= 0 || !validText(project.name, 16) || ids.contains(project.id) || names.contains(project.name))
            return QStringLiteral("项目ID/名称为空、重复或无效。");
        ids.insert(project.id);
        names.insert(project.name);
        QSet<int> types;
        if (project.results.isEmpty())
            return QStringLiteral("项目未配置可用于质控的结果。");
        for (const QcResultRef &result : project.results)
        {
            if (result.type < 1 || result.type > 4 || types.contains(result.type) || !validText(result.name, 16) ||
                !validText(result.unit, 32) || result.decimals < 1 || result.decimals > 6 ||
                !std::isfinite(result.ratio) || result.ratio <= 0)
                return QStringLiteral("项目结果类型仅支持原枚举1至4；名称/单位/倍率/精度须来自已提交项目。");
            types.insert(result.type);
        }
    }
    return QString();
}
}

QcStore::QcStore(QObject *parent) : QObject(parent) {}
QcStore::~QcStore() { release(); }
QString QcStore::referenceKey(qint64 controlId, qint64 projectId) const
{
    return QStringLiteral("dataset:%1:control:%2:project:%3").arg(datasetId_).arg(controlId).arg(projectId);
}
void QcStore::registerTypes()
{
    qRegisterMetaType<QcResultRef>(); qRegisterMetaType<QcResultRefs>();
    qRegisterMetaType<QcProjectRef>(); qRegisterMetaType<QcProjectRefs>();
    qRegisterMetaType<QcRule>(); qRegisterMetaType<QcRules>();
    qRegisterMetaType<QcAssociation>(); qRegisterMetaType<QcAssociations>();
    qRegisterMetaType<QcBatch>(); qRegisterMetaType<QcBatches>();
    qRegisterMetaType<QcControl>(); qRegisterMetaType<QcControls>();
}
QString QcStore::validateControl(const QcControl &control)
{
    if (control.id < 0 || !validName(control.name, 16) || !validName(control.manufacturer, 16))
        return QStringLiteral("质控品名称和厂家须以文字或数字开头，为1至16个字符，不能有首尾空白、路径或控制字符。");
    for (const QcAssociation &association : control.projects)
    {
        const QString message = validateRules(association.rules);
        if (!message.isEmpty()) return message;
    }
    return QString();
}
QString QcStore::validateBatch(const QcBatch &batch)
{
    const QDate date = QDate::fromString(batch.expirationDate, QStringLiteral("yyyy/MM/dd"));
    if (batch.id < 0 || batch.controlId <= 0 || !validName(batch.name, 32) || !date.isValid() ||
        date.toString(QStringLiteral("yyyy/MM/dd")) != batch.expirationDate)
        return QStringLiteral("批号须为1至32个字符；有效期须为真实日期yyyy/MM/dd。");
    return QString();
}
QString QcStore::validateRules(const QcRules &rules)
{
    // \z要求整个输入结束；$会接受数字后的最后一个换行，不能作为持久化校验。
    static const QRegularExpression number(QStringLiteral("^(?:[0-9]{1,4})(?:\\.[0-9]{1,4})?\\z"));
    QSet<qint64> ids;
    for (const QcRule &rule : rules)
    {
        if (rule.id <= 0 || rule.associationId <= 0 || rule.resultType < 1 || rule.resultType > 4 ||
            ids.contains(rule.id) || rule.strategies.size() != 9)
            return QStringLiteral("质控结果ID/关联/类型或策略数量无效。");
        ids.insert(rule.id);
        const QStringList values{rule.upperLimit, rule.lowerLimit, rule.upperBound, rule.lowerBound, rule.sdValue, rule.target};
        for (const QString &value : values)
            if (!value.isEmpty() && (!number.match(value).hasMatch() || value.toDouble() > 9999.0))
                return QStringLiteral("靶值、界限和SD仅保存空值或0至9999、最多4位小数；未启用临床规则。");
        for (int strategy : rule.strategies)
            if (strategy < 0 || strategy > 2)
                return QStringLiteral("策略仅可为原未选0/警告1/结束2；当前仅保存配置。");
    }
    return QString();
}
void QcStore::fail(qint64 requestId, const QString &operation, const QString &message)
{
    emit error(requestId, operation, message);
}
bool QcStore::check(qint64 requestId, const QString &operation)
{
    if (QThread::currentThread() != thread())
    {
        fail(requestId, operation, QStringLiteral("质控数据库必须在所属线程调用。"));
        return false;
    }
    if (!ready_)
    {
        fail(requestId, operation, QStringLiteral("质控数据库尚未就绪。"));
        return false;
    }
    return true;
}
bool QcStore::checkReferences(qint64 requestId, const QString &operation)
{
    if (!check(requestId, operation)) return false;
    if (referencesReady_) return true;
    if (bindingAttempted_)
    {
        fail(requestId, operation, QStringLiteral("项目引用绑定/对账未通过；保留本地数据，需修复后重新绑定。"));
        return false;
    }
    QString message;
    QSqlQuery query(database_);
    if (!run(&query, "SELECT EXISTS(SELECT 1 FROM quality_item_proj) OR EXISTS(SELECT 1 FROM quality_data) OR EXISTS(SELECT 1 FROM qc_project_snapshot) OR EXISTS(SELECT 1 FROM quality_rules)", {}, &message)
        || !query.next())
    { fail(requestId, operation, message); return false; }
    if (query.value(0).toBool())
    {
        fail(requestId, operation, QStringLiteral("已有质控关联/历史尚未完成项目引用对账；请先重新绑定项目库，数据保留。"));
        return false;
    }
    return true;
}
bool QcStore::execute(const QString &sql, const QVariantList &values, QString *message, int expected)
{
    QSqlQuery query(database_);
    if (!run(&query, sql, values, message)) return false;
    if (expected >= 0 && query.numRowsAffected() != expected)
    {
        *message = QStringLiteral("数据已变化或记录不存在；本次修改已取消。");
        return false;
    }
    return true;
}
bool QcStore::begin(QString *message)
{
    return execute(QStringLiteral("BEGIN IMMEDIATE"), {}, message);
}
bool QcStore::commit(QString *message)
{
    if (database_.commit()) return true;
    *message = database_.lastError().text();
    database_.rollback();
    return false;
}
bool QcStore::controlName(qint64 id, QString *name, QString *message)
{
    QSqlQuery query(database_);
    if (!run(&query, QStringLiteral("SELECT item_name FROM quality_control_list WHERE id=?"), {id}, message)) return false;
    if (!query.next()) { *message = QStringLiteral("质控品不存在。"); return false; }
    *name = query.value(0).toString();
    return true;
}
bool QcStore::historyExists(const QString &where, const QVariantList &values, bool *exists, QString *message)
{
    QSqlQuery query(database_);
    if (!run(&query, QStringLiteral("SELECT 1 FROM quality_data WHERE ") + where + QStringLiteral(" LIMIT 1"), values, message)) return false;
    *exists = query.next();
    return true;
}
void QcStore::initialize(QString directory)
{
    if (QThread::currentThread() != thread()) { fail(0, "initialize", QStringLiteral("线程错误。")); return; }
    release();
    directory = QDir::cleanPath(directory);
    if (!safeDirectory(directory)) { fail(0, "initialize", QStringLiteral("仅接受显式匿名绝对目录，禁止参考目录及符号链接。")); return; }
    QDir dir(directory);
    if (!QFileInfo::exists(dir.filePath(marker)) && dir.exists() &&
        !dir.entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty())
    { fail(0, "initialize", QStringLiteral("非空未标记目录禁止建立质控库。")); return; }
    if (!dir.mkpath(".")) { fail(0, "initialize", QStringLiteral("无法建立质控目录。")); return; }
    const QString path = dir.filePath("quality_control.db");
    const bool fresh = !QFileInfo::exists(path);
    if (QFileInfo(path).isSymLink()) { fail(0, "initialize", QStringLiteral("数据库不能是符号链接。")); return; }
    connectionName_ = QStringLiteral("Qc-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    database_ = QSqlDatabase::addDatabase("QSQLITE", connectionName_);
    database_.setDatabaseName(path);
    database_.setConnectOptions("QSQLITE_BUSY_TIMEOUT=100");
    if (!database_.open()) { fail(0, "initialize", database_.lastError().text()); release(); return; }
    QString message;
    if (!fresh)
    {
        QSqlQuery query(database_);
        if (!run(&query, "SELECT value FROM qc_metadata WHERE key='schema'", {}, &message) || !query.next() || query.value(0).toString() != schema)
        { fail(0, "initialize", QStringLiteral("现有文件不是本模块匿名结构，保留原文件。")); release(); return; }
    }
    const QStringList statements{
        "PRAGMA foreign_keys=ON",
        "CREATE TABLE IF NOT EXISTS qc_metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL)",
        "CREATE TABLE IF NOT EXISTS quality_control_list(id INTEGER PRIMARY KEY AUTOINCREMENT,item_name VARCHAR NOT NULL UNIQUE,is_used INTEGER NOT NULL CHECK(is_used IN(0,1)),factor_name VARCHAR NOT NULL)",
        "CREATE TABLE IF NOT EXISTS quality_item_proj(id INTEGER PRIMARY KEY AUTOINCREMENT,item_id INTEGER NOT NULL REFERENCES quality_control_list(id) ON DELETE RESTRICT,proj_name VARCHAR NOT NULL,project_id INTEGER NOT NULL,UNIQUE(item_id,project_id))",
        "CREATE TABLE IF NOT EXISTS qc_project_snapshot(association_id INTEGER NOT NULL REFERENCES quality_item_proj(id) ON DELETE RESTRICT,result_type INTEGER NOT NULL,result_name TEXT NOT NULL,unit TEXT NOT NULL,ratio REAL NOT NULL,decimals INTEGER NOT NULL,PRIMARY KEY(association_id,result_type))",
        "CREATE TABLE IF NOT EXISTS quality_rules(id INTEGER PRIMARY KEY AUTOINCREMENT,item_proj_id INTEGER NOT NULL REFERENCES quality_item_proj(id) ON DELETE RESTRICT,type INTEGER NOT NULL,name VARCHAR NOT NULL,used INTEGER NOT NULL,upper_limit VARCHAR,lower_limit VARCHAR,upper_bound VARCHAR,lower_bound VARCHAR,sd_value VARCHAR,target VARCHAR,r1_2s INTEGER NOT NULL,r1_3s INTEGER NOT NULL,r2_2s INTEGER NOT NULL,r4_1s INTEGER NOT NULL,r5_5s INTEGER NOT NULL,rR4s INTEGER NOT NULL,r7T INTEGER NOT NULL,r10X INTEGER NOT NULL,limit_control INTEGER NOT NULL,UNIQUE(item_proj_id,type))",
        "CREATE TABLE IF NOT EXISTS quality_control_batch(id INTEGER PRIMARY KEY AUTOINCREMENT,fid INTEGER NOT NULL REFERENCES quality_control_list(id) ON DELETE RESTRICT,batch_name VARCHAR UNIQUE,expiration_date VARCHAR,cur_batch INTEGER NOT NULL CHECK(cur_batch IN(0,1)),prv_batch INTEGER NOT NULL CHECK(prv_batch IN(0,1)),CHECK(NOT(cur_batch=1 AND prv_batch=1)))",
        "CREATE TABLE IF NOT EXISTS quality_data(id INTEGER PRIMARY KEY AUTOINCREMENT,control_name VARCHAR NOT NULL,project_name VARCHAR NOT NULL,control_batch VARCHAR NOT NULL,result_val VARCHAR NOT NULL,date VARCHAR NOT NULL,time VARCHAR NOT NULL)"
    };
    if (!begin(&message)) { fail(0, "initialize", message); release(); return; }
    bool ok = true;
    for (const QString &sql : statements) if (!(ok = execute(sql, {}, &message))) break;
    if (ok && fresh) ok = execute("INSERT INTO qc_metadata VALUES('schema',?)", {schema}, &message);
    if (ok && fresh) ok = execute("INSERT INTO qc_metadata VALUES('dataset_uuid',?)", {QUuid::createUuid().toString(QUuid::WithoutBraces)}, &message);
    if (!ok || !commit(&message)) { database_.rollback(); fail(0, "initialize", message); release(); return; }
    // PRAGMA foreign_keys在事务中不生效，必须在建表提交后启用。
    if (!execute("PRAGMA foreign_keys=ON", {}, &message)) { fail(0, "initialize", message); release(); return; }
    QSqlQuery identity(database_);
    if (!run(&identity, "SELECT value FROM qc_metadata WHERE key='dataset_uuid'", {}, &message) || !identity.next() || QUuid(identity.value(0).toString()).isNull())
    { fail(0, "initialize", QStringLiteral("质控数据集身份缺失或无效；保留文件待核对，不猜测已有预约归属。")); release(); return; }
    datasetId_ = identity.value(0).toString();
    identity.finish();
    QFile tag(dir.filePath(marker));
    if (!tag.open(QIODevice::WriteOnly | QIODevice::Truncate) || tag.write(schema.toUtf8()) != schema.toUtf8().size())
    { fail(0, "initialize", QStringLiteral("无法保存匿名质控标记。")); release(); return; }
    tag.close();
    ready_ = true;
    emit ready(path);
}
void QcStore::bindCatalog(QString directory)
{
    if (!check(0, "bindCatalog")) return;
    bindingAttempted_ = true;
    referencesReady_ = false;
    // 每次明确换库先撤销旧连接，失败后不能悄悄继续向旧项目库预约。
    if (catalogDatabase_.isValid())
    {
        catalogDatabase_.close(); catalogDatabase_ = QSqlDatabase(); QSqlDatabase::removeDatabase(catalogConnectionName_);
        catalogConnectionName_.clear();
    }
    directory = QDir::cleanPath(directory);
    if (!safeDirectory(directory) || !QFileInfo::exists(QDir(directory).filePath(".sn100-catalog-offline-v1")))
    { fail(0, "bindCatalog", QStringLiteral("项目库必须是已标记的匿名绝对目录。")); return; }
    const QString path = QDir(directory).filePath("catalog.db");
    if (!QFileInfo::exists(path) || QFileInfo(path).isSymLink()) { fail(0, "bindCatalog", QStringLiteral("匿名项目库不存在或为符号链接。")); return; }
    catalogConnectionName_ = QStringLiteral("QcCatalog-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    catalogDatabase_ = QSqlDatabase::addDatabase("QSQLITE", catalogConnectionName_);
    catalogDatabase_.setDatabaseName(path);
    catalogDatabase_.setConnectOptions("QSQLITE_BUSY_TIMEOUT=100");
    QString message;
    QSqlQuery query(catalogDatabase_);
    bool ok = catalogDatabase_.open();
    if (ok) ok = run(&query, "SELECT value FROM catalog_metadata WHERE key='schema'", {}, &message) && query.next() && query.value(0).toString() == "sn100-catalog-offline-v1";
    if (ok) ok = run(&query, "SELECT owner_module,reference_key,project_id FROM catalog_project_reference LIMIT 0", {}, &message);
    if (ok) ok = run(&query, "PRAGMA foreign_keys=ON", {}, &message);
    query.finish();
    if (ok) ok = reconcileReferences(&message);
    if (!ok) { query.finish(); catalogDatabase_.close(); fail(0, "bindCatalog", QStringLiteral("项目库未就绪或引用协议不可用：") + message); return; }
    referencesReady_ = true;
    emit catalogBound(path);
}
bool QcStore::validateHistoryReferences(QString *message)
{
    QSqlQuery query(database_);
    if (!run(&query, "SELECT d.id FROM quality_data d WHERE NOT EXISTS("
             "SELECT 1 FROM quality_control_list c JOIN quality_item_proj a ON a.item_id=c.id "
             "JOIN quality_control_batch b ON b.fid=c.id WHERE c.item_name=d.control_name "
             "AND a.proj_name=d.project_name AND b.batch_name=d.control_batch) LIMIT 1", {}, message)) return false;
    if (query.next())
    {
        *message = QStringLiteral("质控历史%1缺少对应控制品/项目关联/批次；保留历史，不能按名称猜测新身份。")
            .arg(query.value(0).toLongLong());
        return false;
    }
    return true;
}
bool QcStore::reconcileReferences(QString *message)
{
    // 同一个数据集的所有写者都先锁本地文件，再访问catalog，覆盖停用控制品和全部历史。
    if (!begin(message)) return false;
    bool ok = true;
    {
        QSqlQuery integrity(database_);
        ok = run(&integrity, "PRAGMA foreign_key_check", {}, message);
        if (ok && integrity.next())
        { ok = false; *message = QStringLiteral("质控本地表关系损坏：%1；需先核对恢复。").arg(integrity.value(0).toString()); }
    }
    if (ok) ok = validateHistoryReferences(message);
    QSqlQuery catalogQuery(catalogDatabase_);
    if (ok) ok = run(&catalogQuery, "BEGIN IMMEDIATE", {}, message);
    if (!ok) { database_.rollback(); return false; }
    const QString pathKey = QStringLiteral("quality_control_dataset_path:") + datasetId_;
    const QString canonicalDatabase = QFileInfo(database_.databaseName()).canonicalFilePath();
    bool registerPath = false;
    if (canonicalDatabase.isEmpty()) *message = QStringLiteral("质控数据文件无法解析为规范物理路径，未登记引用归属。");
    ok = !canonicalDatabase.isEmpty()
        && run(&catalogQuery, "SELECT value FROM catalog_metadata WHERE key=?", {pathKey}, message);
    if (ok && catalogQuery.next())
    {
        if (catalogQuery.value(0).toString() != canonicalDatabase)
        {
            ok = false;
            *message = QStringLiteral("此质控数据集UUID已绑定另一物理文件：%1；当前%2。保留副本及预约，需停写核对恢复/迁移后显式更新绑定。")
                .arg(catalogQuery.value(0).toString(), canonicalDatabase);
        }
    }
    else if (ok) registerPath = true;
    QSet<QString> expectedKeys;
    QSqlQuery associations(database_);
    if (ok) ok = run(&associations, "SELECT a.id,a.item_id,a.project_id,a.proj_name,c.id FROM quality_item_proj a "
             "LEFT JOIN quality_control_list c ON c.id=a.item_id ORDER BY a.id", {}, message);
    while (ok && associations.next())
    {
        const qint64 associationId = associations.value(0).toLongLong();
        const qint64 controlId = associations.value(1).toLongLong();
        QcProjectRef project; project.id = associations.value(2).toLongLong(); project.name = associations.value(3).toString();
        if (associationId <= 0 || controlId <= 0 || associations.value(4).isNull())
        { ok = false; *message = QStringLiteral("质控项目关联%1的控制品身份缺失或无效。").arg(associationId); break; }
        QSqlQuery snapshots(database_);
        ok = run(&snapshots, "SELECT result_type,result_name,unit,ratio,decimals FROM qc_project_snapshot WHERE association_id=? ORDER BY result_type", {associationId}, message);
        while (ok && snapshots.next())
        {
            QcResultRef result; result.type = snapshots.value(0).toInt(); result.name = snapshots.value(1).toString();
            result.unit = snapshots.value(2).toString(); result.ratio = snapshots.value(3).toDouble();
            result.decimals = snapshots.value(4).toInt(); project.results.append(result);
        }
        if (!ok) break;
        *message = validateCatalog({project});
        if (!message->isEmpty()) { ok = false; *message = QStringLiteral("质控关联%1历史快照无效：").arg(associationId) + *message; break; }
        ok = run(&catalogQuery, "SELECT project_en_name FROM project_base WHERE id=?", {project.id}, message)
            && catalogQuery.next() && catalogQuery.value(0).toString() == project.name;
        if (!ok) { *message = QStringLiteral("质控关联%1的项目%2已缺失或改名；未更换稳定身份。").arg(associationId).arg(project.id); break; }
        QSqlQuery rules(database_);
        ok = run(&rules, "SELECT type,name FROM quality_rules WHERE item_proj_id=? ORDER BY type", {associationId}, message);
        int ruleCount = 0;
        while (ok && rules.next())
        {
            bool found = false;
            for (const QcResultRef &result : project.results)
                if (result.type == rules.value(0).toInt() && result.name == rules.value(1).toString()) { found = true; break; }
            if (!found) { ok = false; *message = QStringLiteral("质控关联%1规则与历史结果快照身份不一致。").arg(associationId); break; }
            ++ruleCount;
        }
        if (ok && ruleCount != project.results.size())
        { ok = false; *message = QStringLiteral("质控关联%1缺少规则或历史结果快照。").arg(associationId); }
        for (const QcResultRef &result : project.results)
        {
            if (!ok) break;
            static const QStringList types{QString(), "Original", "Calibration", "Rate", "INR"};
            ok = run(&catalogQuery, "SELECT 1 FROM project_result WHERE project_id=? AND result_type=?", {project.id,types.value(result.type)}, message) && catalogQuery.next();
            if (!ok) *message = QStringLiteral("质控关联%1当前项目缺少原结果类型%2；历史单位快照保留。").arg(associationId).arg(result.type);
        }
        if (!ok) break;
        // 历史快照只核结构和结果身份，不用当前单位/名称/倍率/精度覆写既存靶值含义。
        const QString key = referenceKey(controlId, project.id);
        expectedKeys.insert(key);
        ok = run(&catalogQuery, "SELECT project_id FROM catalog_project_reference WHERE owner_module='quality_control' AND reference_key=?", {key}, message);
        if (ok && catalogQuery.next())
        {
            if (catalogQuery.value(0).toLongLong() != project.id)
            { ok = false; *message = QStringLiteral("质控保护预约身份冲突：%1，保留预约待核对。").arg(key); }
        }
        else if (ok)
            ok = run(&catalogQuery, "INSERT INTO catalog_project_reference(owner_module,reference_key,project_id) VALUES('quality_control',?,?)", {key,project.id}, message);
    }
    if (ok)
    {
        ok = run(&catalogQuery, "SELECT reference_key FROM catalog_project_reference WHERE owner_module='quality_control' AND reference_key LIKE ?",
                 {QStringLiteral("dataset:") + datasetId_ + QStringLiteral(":%")}, message);
        while (ok && catalogQuery.next())
            if (!expectedKeys.contains(catalogQuery.value(0).toString()))
            {
                ok = false;
                *message = QStringLiteral("本质控数据集有无对应关联的保护预约：%1；已保留，需显式核对后释放。")
                    .arg(catalogQuery.value(0).toString());
            }
    }
    if (ok && registerPath)
        ok = run(&catalogQuery, "INSERT INTO catalog_metadata(key,value) VALUES(?,?)", {pathKey,canonicalDatabase}, message);
    catalogQuery.finish();
    if (!ok || !catalogDatabase_.commit())
    {
        if (ok) *message = catalogDatabase_.lastError().text();
        catalogDatabase_.rollback(); database_.rollback(); return false;
    }
    // 预约已保护真实关联；本地收尾失败时仍保留预约，不能当作新建失败释放。
    if (!commit(message)) { database_.rollback(); return false; }
    return true;
}
void QcStore::setCatalog(qint64 requestId, QcProjectRefs projects)
{
    if (!check(requestId, "catalog")) return;
    const QString message = validateCatalog(projects);
    if (!message.isEmpty()) { fail(requestId, "catalog", message); return; }
    catalog_ = projects;
    emit catalogReady(requestId, catalog_);
}
void QcStore::load(qint64 requestId)
{
    if (!checkReferences(requestId, "load")) return;
    QString message;
    if (!database_.transaction()) { fail(requestId, "load", database_.lastError().text()); return; }
    QcControls controls;
    QSqlQuery query(database_);
    bool ok = run(&query, "SELECT id,item_name,factor_name,is_used FROM quality_control_list ORDER BY id", {}, &message);
    while (ok && query.next())
    {
        QcControl control;
        control.id = query.value(0).toLongLong(); control.name = query.value(1).toString();
        control.manufacturer = query.value(2).toString(); control.used = query.value(3).toBool();
        QSqlQuery associations(database_);
        ok = run(&associations, "SELECT id,project_id,proj_name FROM quality_item_proj WHERE item_id=? ORDER BY id", {control.id}, &message);
        while (ok && associations.next())
        {
            QcAssociation association;
            association.id = associations.value(0).toLongLong(); association.controlId = control.id;
            association.projectId = associations.value(1).toLongLong(); association.projectName = associations.value(2).toString();
            QSqlQuery refs(database_);
            ok = run(&refs, "SELECT result_type,result_name,unit,ratio,decimals FROM qc_project_snapshot WHERE association_id=? ORDER BY result_type", {association.id}, &message);
            while (ok && refs.next())
            {
                QcResultRef ref; ref.type = refs.value(0).toInt(); ref.name = refs.value(1).toString();
                ref.unit = refs.value(2).toString(); ref.ratio = refs.value(3).toDouble(); ref.decimals = refs.value(4).toInt(); association.resultRefs.append(ref);
            }
            QSqlQuery rules(database_);
            if (ok) ok = run(&rules, "SELECT id,type,name,used,upper_limit,lower_limit,upper_bound,lower_bound,sd_value,target,r1_2s,r1_3s,r2_2s,r4_1s,r5_5s,rR4s,r7T,r10X,limit_control FROM quality_rules WHERE item_proj_id=? ORDER BY type", {association.id}, &message);
            while (ok && rules.next())
            {
                QcRule rule;
                rule.id = rules.value(0).toLongLong(); rule.associationId = association.id;
                rule.resultType = rules.value(1).toInt(); rule.name = rules.value(2).toString(); rule.used = rules.value(3).toBool();
                rule.upperLimit = rules.value(4).toString(); rule.lowerLimit = rules.value(5).toString();
                rule.upperBound = rules.value(6).toString(); rule.lowerBound = rules.value(7).toString();
                rule.sdValue = rules.value(8).toString(); rule.target = rules.value(9).toString();
                rule.strategies.clear(); for (int i = 10; i < 19; ++i) rule.strategies.append(rules.value(i).toInt());
                association.rules.append(rule);
            }
            control.projects.append(association);
        }
        QSqlQuery batches(database_);
        if (ok) ok = run(&batches, "SELECT id,batch_name,expiration_date,cur_batch,prv_batch FROM quality_control_batch WHERE fid=? ORDER BY id", {control.id}, &message);
        while (ok && batches.next())
        {
            QcBatch batch; batch.id = batches.value(0).toLongLong(); batch.controlId = control.id;
            batch.name = batches.value(1).toString(); batch.expirationDate = batches.value(2).toString();
            batch.current = batches.value(3).toBool(); batch.previous = batches.value(4).toBool(); control.batches.append(batch);
        }
        controls.append(control);
    }
    if (!ok || !commit(&message)) { database_.rollback(); fail(requestId, "load", message); return; }
    emit snapshotReady(requestId, controls);
}
void QcStore::saveControl(qint64 requestId, QcControl control)
{
    const QString operation = "saveControl";
    if (!checkReferences(requestId, operation)) return;
    QString message = validateControl(control);
    if (!message.isEmpty()) { fail(requestId, operation, message); return; }
    if (!begin(&message)) { fail(requestId, operation, message); return; }
    bool ok = true;
    if (control.id == 0)
    {
        if (!control.projects.isEmpty()) { message = QStringLiteral("先保存新质控品，再明确选择项目。"); ok = false; }
        if (ok) ok = execute("INSERT INTO quality_control_list(item_name,is_used,factor_name) VALUES(?,1,?)", {control.name, control.manufacturer}, &message, 1);
        if (ok) { QSqlQuery query(database_); query.exec("SELECT last_insert_rowid()"); query.next(); control.id = query.value(0).toLongLong(); }
    }
    else
    {
        QString oldName; bool referenced = false;
        ok = controlName(control.id, &oldName, &message);
        if (ok && oldName != control.name) ok = historyExists("control_name=?", {oldName}, &referenced, &message);
        if (ok && referenced) { message = QStringLiteral("该质控品已有历史结果，不能改名；历史名称保持原值。"); ok = false; }
        if (ok) ok = execute("UPDATE quality_control_list SET item_name=?,factor_name=? WHERE id=?", {control.name, control.manufacturer, control.id}, &message, 1);
        for (const QcAssociation &association : control.projects)
        {
            if (!ok) break;
            QSqlQuery belongs(database_);
            ok = run(&belongs, "SELECT project_id,proj_name FROM quality_item_proj WHERE id=? AND item_id=?", {association.id,control.id}, &message) && belongs.next();
            if (ok && (belongs.value(0).toLongLong() != association.projectId || belongs.value(1).toString() != association.projectName)) ok = false;
            if (!ok) { message = QStringLiteral("项目关联已变化，请重载后再保存草稿。"); break; }
            for (const QcRule &rule : association.rules)
            {
                if (rule.associationId != association.id) { ok = false; message = QStringLiteral("规则关联ID不匹配。"); break; }
                QVariantList values = ruleValues(rule);
                values << rule.id << association.id << rule.resultType << rule.name;
                ok = execute("UPDATE quality_rules SET " + ruleColumns + " WHERE id=? AND item_proj_id=? AND type=? AND name=?", values, &message, 1);
                if (!ok) break;
            }
        }
    }
    if (!ok || !commit(&message)) { database_.rollback(); fail(requestId, operation, message); return; }
    emit succeeded(requestId, operation, control.id);
}
void QcStore::removeControl(qint64 requestId, qint64 controlId)
{
    const QString operation = "removeControl";
    if (!checkReferences(requestId, operation)) return;
    QString message, name; bool referenced = false;
    if (!begin(&message)) { fail(requestId, operation, message); return; }
    bool ok = controlName(controlId, &name, &message) && historyExists("control_name=?", {name}, &referenced, &message);
    if (ok && referenced) { ok = false; message = QStringLiteral("质控品已有历史结果，不能删除。"); }
    QStringList keys;
    QSqlQuery query(database_);
    if (ok) ok = run(&query, "SELECT project_id FROM quality_item_proj WHERE item_id=?", {controlId}, &message);
    while (ok && query.next()) keys.append(referenceKey(controlId, query.value(0).toLongLong()));
    if (ok) ok = execute("DELETE FROM quality_rules WHERE item_proj_id IN(SELECT id FROM quality_item_proj WHERE item_id=?)", {controlId}, &message);
    if (ok) ok = execute("DELETE FROM qc_project_snapshot WHERE association_id IN(SELECT id FROM quality_item_proj WHERE item_id=?)", {controlId}, &message);
    if (ok) ok = execute("DELETE FROM quality_item_proj WHERE item_id=?", {controlId}, &message);
    if (ok) ok = execute("DELETE FROM quality_control_batch WHERE fid=?", {controlId}, &message);
    if (ok) ok = execute("DELETE FROM quality_control_list WHERE id=?", {controlId}, &message, 1);
    if (!ok || !commit(&message)) { database_.rollback(); fail(requestId, operation, message); return; }
    emit referenceCheckpoint(QStringLiteral("after-detach-commit"), controlId);
    if (!keys.isEmpty() && !releaseReferences(keys, &message)) emit warning(QStringLiteral("质控品已删除；项目保留保护预约，需重试核对释放：") + message);
    emit succeeded(requestId, operation, controlId);
}
bool QcStore::reserveProjects(qint64 controlId, const QcProjectRefs &projects, QStringList *newKeys, QString *message)
{
    newKeys->clear();
    if (!catalogDatabase_.isOpen()) { *message = QStringLiteral("项目引用保护尚未绑定；禁止保存关联。"); return false; }
    QSqlQuery query(catalogDatabase_);
    if (!run(&query, "BEGIN IMMEDIATE", {}, message)) return false;
    bool ok = true;
    for (const QcProjectRef &project : projects)
    {
        ok = run(&query, "SELECT project_en_name FROM project_base WHERE id=?", {project.id}, message) && query.next() && query.value(0).toString() == project.name;
        if (!ok) { *message = QStringLiteral("项目ID/名称已变化，请重新加载。"); break; }
        for (const QcResultRef &result : project.results)
        {
            // 原ProjectsStore使用文本枚举键；以下映射与其已保存值一致，未知键不猜测。
            static const QStringList keys{QString(), "Original", "Calibration", "Rate", "INR"};
            ok = run(&query, "SELECT r.result_name,r.decimal_point,r.unit_name,u.ratio FROM project_result r JOIN option_unit u ON u.unit=r.unit_name WHERE r.project_id=? AND r.result_type=?", {project.id, keys.value(result.type)}, message) && query.next();
            if (!ok || query.value(0).toString() != result.name || query.value(1).toInt() != result.decimals || query.value(2).toString() != result.unit || std::fabs(query.value(3).toDouble() - result.ratio) > 1e-12)
            { ok = false; *message = QStringLiteral("项目结果/单位/倍率/精度已变化或不支持，请重新加载。"); break; }
        }
        if (!ok) break;
        const QString key = referenceKey(controlId, project.id);
        ok = run(&query, "SELECT project_id FROM catalog_project_reference WHERE owner_module='quality_control' AND reference_key=?", {key}, message);
        if (!ok) break;
        if (query.next())
        {
            if (query.value(0).toLongLong() != project.id) { ok = false; *message = QStringLiteral("已有项目预约归属冲突，需人工核对。"); break; }
        }
        else
        {
            ok = run(&query, "INSERT INTO catalog_project_reference(owner_module,reference_key,project_id) VALUES('quality_control',?,?)", {key,project.id}, message);
            if (!ok) break;
            newKeys->append(key);
        }
    }
    if (!ok || !catalogDatabase_.commit())
    {
        if (ok) *message = catalogDatabase_.lastError().text();
        catalogDatabase_.rollback(); newKeys->clear(); return false;
    }
    return true;
}
bool QcStore::releaseReferences(const QStringList &keys, QString *message)
{
    if (keys.isEmpty()) return true;
    if (!catalogDatabase_.isOpen()) { *message = QStringLiteral("项目库未绑定。"); return false; }
    // 调用者可能已经提交或被SQLite自动ROLLBACK；一律重新拿本地写锁并核真实关联。
    // 在此锁保护下回收，不会删除另一个写者解除后又重建所需的同组合预约。
    if (!begin(message)) return false;
    if (!validateHistoryReferences(message)) { database_.rollback(); return false; }
    const QRegularExpression pattern(QStringLiteral("\\Adataset:") + QRegularExpression::escape(datasetId_)
        + QStringLiteral(":control:([1-9][0-9]*):project:([1-9][0-9]*)\\z"));
    QStringList absentKeys;
    for (const QString &key : keys)
    {
        const auto match = pattern.match(key);
        bool validControl = false, validProject = false;
        const qint64 controlId = match.captured(1).toLongLong(&validControl);
        const qint64 projectId = match.captured(2).toLongLong(&validProject);
        if (!match.hasMatch() || !validControl || !validProject)
        { *message = QStringLiteral("回收键不属于本数据集或编号无效；未删除预约。"); database_.rollback(); return false; }
        QSqlQuery existing(database_);
        if (!run(&existing, "SELECT 1 FROM quality_item_proj WHERE item_id=? AND project_id=? LIMIT 1", {controlId,projectId}, message))
        { database_.rollback(); return false; }
        if (!existing.next()) absentKeys.append(key);
    }
    if (absentKeys.isEmpty()) return commit(message);
    QSqlQuery query(catalogDatabase_);
    if (!run(&query, "BEGIN IMMEDIATE", {}, message)) { database_.rollback(); return false; }
    bool ok = true;
    for (const QString &key : absentKeys)
        if (!(ok = run(&query, "DELETE FROM catalog_project_reference WHERE owner_module='quality_control' AND reference_key=?", {key}, message))) break;
    if (!ok || !catalogDatabase_.commit())
    {
        if (ok) *message = catalogDatabase_.lastError().text();
        catalogDatabase_.rollback(); database_.rollback(); return false;
    }
    return commit(message);
}
void QcStore::setProjects(qint64 requestId, qint64 controlId, QcProjectRefs projects)
{
    const QString operation = "setProjects";
    if (!checkReferences(requestId, operation)) return;
    if (!referencesReady_) { fail(requestId, operation, QStringLiteral("项目引用尚未完成绑定对账，不能保存关联。")); return; }
    QString message = validateCatalog(projects);
    if (!message.isEmpty()) { fail(requestId, operation, message); return; }
    QString name;
    for (const QcProjectRef &project : projects)
    {
        bool found = false;
        for (const QcProjectRef &known : catalog_) if (known.id == project.id && known.name == project.name) { found = true; break; }
        if (!found) { fail(requestId, operation, QStringLiteral("项目未在已提交快照中，请重新加载。")); return; }
    }
    if (!begin(&message))
    {
        fail(requestId, operation, message); return;
    }
    if (!controlName(controlId, &name, &message)) { database_.rollback(); fail(requestId, operation, message); return; }
    QStringList newKeys;
    if (!reserveProjects(controlId, projects, &newKeys, &message))
    { database_.rollback(); fail(requestId, operation, message); return; }
    QSet<qint64> selected; for (const QcProjectRef &project : projects) selected.insert(project.id);
    QSqlQuery query(database_);
    bool ok = run(&query, "SELECT id,project_id,proj_name FROM quality_item_proj WHERE item_id=?", {controlId}, &message);
    QSet<qint64> existing;
    QStringList removedKeys;
    QList<qint64> removeIds;
    while (ok && query.next())
    {
        const qint64 id = query.value(0).toLongLong(), projectId = query.value(1).toLongLong();
        existing.insert(projectId);
        if (!selected.contains(projectId))
        {
            bool referenced = false;
            ok = historyExists("control_name=? AND project_name=?", {name, query.value(2)}, &referenced, &message);
            if (ok && referenced) { ok = false; message = QStringLiteral("项目已有质控历史，不能解除关联。"); }
            removeIds.append(id); removedKeys.append(referenceKey(controlId, projectId));
        }
    }
    for (qint64 id : removeIds)
    {
        if (!ok) break;
        ok = execute("DELETE FROM quality_rules WHERE item_proj_id=?", {id}, &message) &&
             execute("DELETE FROM qc_project_snapshot WHERE association_id=?", {id}, &message) &&
             execute("DELETE FROM quality_item_proj WHERE id=? AND item_id=?", {id, controlId}, &message, 1);
    }
    for (const QcProjectRef &project : projects)
    {
        if (!ok) break;
        if (existing.contains(project.id)) continue;
        QSqlQuery insert(database_);
        ok = run(&insert, "INSERT INTO quality_item_proj(item_id,proj_name,project_id) VALUES(?,?,?)", {controlId, project.name, project.id}, &message);
        const qint64 associationId = insert.lastInsertId().toLongLong();
        for (const QcResultRef &result : project.results)
        {
            if (!ok) break;
            ok = execute("INSERT INTO quality_rules(item_proj_id,type,name,used,upper_limit,lower_limit,upper_bound,lower_bound,sd_value,target,r1_2s,r1_3s,r2_2s,r4_1s,r5_5s,rR4s,r7T,r10X,limit_control) VALUES(?,?,?,1,'','','','','','',0,0,0,0,0,0,0,0,0)", {associationId, result.type, result.name}, &message, 1);
            if (ok) ok = execute("INSERT INTO qc_project_snapshot(association_id,result_type,result_name,unit,ratio,decimals) VALUES(?,?,?,?,?,?)", {associationId,result.type,result.name,result.unit,result.ratio,result.decimals}, &message, 1);
        }
    }
    if (!ok || !commit(&message))
    {
        database_.rollback();
        emit referenceCheckpoint(QStringLiteral("after-failed-write"), controlId);
        QString cleanup;
        if (!releaseReferences(newKeys, &cleanup)) emit warning(QStringLiteral("本次新增预约保留待核对：") + cleanup);
        fail(requestId, operation, message); return;
    }
    emit referenceCheckpoint(QStringLiteral("after-detach-commit"), controlId);
    if (!releaseReferences(removedKeys, &message)) emit warning(QStringLiteral("关联已解除；项目保留保护预约待核对：") + message);
    emit succeeded(requestId, operation, controlId);
}
void QcStore::saveRules(qint64 requestId, qint64 associationId, QcRules rules)
{
    const QString operation = "saveRules";
    if (!checkReferences(requestId, operation)) return;
    QString message = validateRules(rules);
    if (rules.isEmpty()) message = QStringLiteral("没有可保存的规则配置。");
    if (!message.isEmpty()) { fail(requestId, operation, message); return; }
    if (!begin(&message)) { fail(requestId, operation, message); return; }
    bool ok = true;
    for (const QcRule &rule : rules)
    {
        if (rule.associationId != associationId) { message = QStringLiteral("规则不属于所选项目。"); ok = false; break; }
        QVariantList values = ruleValues(rule); values << rule.id << associationId << rule.resultType << rule.name;
        if (!(ok = execute("UPDATE quality_rules SET " + ruleColumns + " WHERE id=? AND item_proj_id=? AND type=? AND name=?", values, &message, 1))) break;
    }
    if (!ok || !commit(&message)) { database_.rollback(); fail(requestId, operation, message); return; }
    emit succeeded(requestId, operation, associationId);
}
void QcStore::saveBatch(qint64 requestId, QcBatch batch)
{
    const QString operation = "saveBatch";
    if (!checkReferences(requestId, operation)) return;
    QString message = validateBatch(batch);
    if (!message.isEmpty()) { fail(requestId, operation, message); return; }
    if (!begin(&message)) { fail(requestId, operation, message); return; }
    QString name;
    bool ok = controlName(batch.controlId, &name, &message);
    QSqlQuery query(database_);
    if (ok && batch.id == 0)
    {
        ok = run(&query, "SELECT COUNT(*) FROM quality_control_batch WHERE fid=?", {batch.controlId}, &message) && query.next();
        const bool first = ok && query.value(0).toInt() == 0;
        if (ok) ok = run(&query, "INSERT INTO quality_control_batch(fid,batch_name,expiration_date,cur_batch,prv_batch) VALUES(?,?,?,?,0)", {batch.controlId,batch.name,batch.expirationDate,first ? 1 : 0}, &message);
        if (ok) batch.id = query.lastInsertId().toLongLong();
    }
    else if (ok)
    {
        ok = run(&query, "SELECT batch_name FROM quality_control_batch WHERE id=? AND fid=?", {batch.id,batch.controlId}, &message) && query.next();
        if (!ok) message = QStringLiteral("批次不存在或不属于所选质控品。");
        bool referenced = false;
        if (ok && query.value(0).toString() != batch.name) ok = historyExists("control_name=? AND control_batch=?", {name,query.value(0)}, &referenced, &message);
        if (ok && referenced) { ok = false; message = QStringLiteral("批次已有历史结果，不能改名。"); }
        if (ok) ok = execute("UPDATE quality_control_batch SET batch_name=?,expiration_date=? WHERE id=? AND fid=?", {batch.name,batch.expirationDate,batch.id,batch.controlId}, &message, 1);
    }
    if (!ok || !commit(&message)) { database_.rollback(); fail(requestId, operation, message); return; }
    emit succeeded(requestId, operation, batch.id);
}
void QcStore::removeBatch(qint64 requestId, qint64 batchId)
{
    const QString operation = "removeBatch";
    if (!checkReferences(requestId, operation)) return;
    QString message;
    if (!begin(&message)) { fail(requestId, operation, message); return; }
    QSqlQuery query(database_);
    bool ok = run(&query, "SELECT b.batch_name,b.cur_batch,b.prv_batch,c.item_name FROM quality_control_batch b JOIN quality_control_list c ON c.id=b.fid WHERE b.id=?", {batchId}, &message) && query.next();
    if (!ok) message = QStringLiteral("批次不存在。");
    if (ok && (query.value(1).toBool() || query.value(2).toBool())) { ok = false; message = QStringLiteral("当前/前一批次不能删除；请先明确切换批次。"); }
    bool referenced = false;
    if (ok) ok = historyExists("control_name=? AND control_batch=?", {query.value(3),query.value(0)}, &referenced, &message);
    if (ok && referenced) { ok = false; message = QStringLiteral("批次已有历史结果，不能删除。"); }
    if (ok) ok = execute("DELETE FROM quality_control_batch WHERE id=?", {batchId}, &message, 1);
    if (!ok || !commit(&message)) { database_.rollback(); fail(requestId, operation, message); return; }
    emit succeeded(requestId, operation, batchId);
}
void QcStore::switchBatch(qint64 requestId, qint64 controlId, qint64 batchId)
{
    const QString operation = "switchBatch";
    if (!checkReferences(requestId, operation)) return;
    QString message;
    if (!begin(&message)) { fail(requestId, operation, message); return; }
    QSqlQuery query(database_);
    bool ok = run(&query, "SELECT id,cur_batch,prv_batch FROM quality_control_batch WHERE fid=? ORDER BY id", {controlId}, &message);
    int currents = 0, previous = 0; qint64 currentId = 0; bool found = false;
    while (ok && query.next())
    {
        const qint64 id = query.value(0).toLongLong();
        const bool cur = query.value(1).toBool(), prv = query.value(2).toBool();
        if (cur) { ++currents; currentId = id; }
        if (prv) ++previous;
        if (id == batchId) found = true;
        if (cur && prv) { ok = false; message = QStringLiteral("当前/前一批次标志冲突，需要明确修复后再操作。"); }
    }
    if (ok && (!found || currents > 1 || previous > 1)) { ok = false; message = QStringLiteral("批次不存在、归属错误或默认标志多重冲突；未作隐式修复。"); }
    // 重复选择没有任何UPDATE；打开/刷新也不走此用户动作。
    if (ok && currentId != batchId)
    {
        ok = execute("UPDATE quality_control_batch SET cur_batch=0,prv_batch=0 WHERE fid=?", {controlId}, &message);
        if (ok && currentId) ok = execute("UPDATE quality_control_batch SET prv_batch=1 WHERE id=? AND fid=?", {currentId,controlId}, &message, 1);
        if (ok) ok = execute("UPDATE quality_control_batch SET cur_batch=1,prv_batch=0 WHERE id=? AND fid=?", {batchId,controlId}, &message, 1);
    }
    if (!ok || !commit(&message)) { database_.rollback(); fail(requestId, operation, message); return; }
    emit succeeded(requestId, operation, batchId);
}
void QcStore::inspectProjectReference(qint64 requestId, qint64 projectId)
{
    if (!check(requestId, "inspectProjectReference")) return;
    QString message; QSqlQuery query(database_);
    if (!run(&query, "SELECT 1 FROM quality_item_proj WHERE project_id=? LIMIT 1", {projectId}, &message)) { fail(requestId, "inspectProjectReference", message); return; }
    emit projectReferenceFound(requestId, projectId, query.next());
}
void QcStore::release()
{
    ready_ = false; referencesReady_ = false; bindingAttempted_ = false; catalog_.clear(); datasetId_.clear();
    if (catalogDatabase_.isValid()) { catalogDatabase_.close(); catalogDatabase_ = QSqlDatabase(); }
    if (!catalogConnectionName_.isEmpty()) { QSqlDatabase::removeDatabase(catalogConnectionName_); catalogConnectionName_.clear(); }
    if (database_.isValid()) { database_.close(); database_ = QSqlDatabase(); }
    if (!connectionName_.isEmpty()) { QSqlDatabase::removeDatabase(connectionName_); connectionName_.clear(); }
}
void QcStore::close()
{
    if (QThread::currentThread() != thread()) { fail(0, "close", QStringLiteral("关闭线程错误。")); return; }
    release(); emit closed();
}
