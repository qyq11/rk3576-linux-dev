#include "reagent_store.h"
#include "original_reagent_editor.h"
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>

ReagentStore::ReagentStore(QObject* parent)
    : QObject(parent)
{
    registerTypes();
}
ReagentStore::~ReagentStore()
{
    close();
}
void ReagentStore::registerTypes()
{
    qRegisterMetaType<AccountSession>();
    qRegisterMetaType<ReagentRecord>();
    qRegisterMetaType<ReagentBottle>();
    qRegisterMetaType<ReagentBatch>();
    qRegisterMetaType<ReagentBatchReference>();
    qRegisterMetaType<ReagentBatchReferences>();
    qRegisterMetaType<ReagentBarcode>();
    qRegisterMetaType<ReagentHole>();
    qRegisterMetaType<ReagentSnapshot>();
    qRegisterMetaType<ReagentProjects>();
}
bool ReagentStore::onWorker(const QString& operation)
{
    if (QThread::currentThread() == thread())
        return true;
    emit error(operation, QStringLiteral("试剂存储线程错误"));
    return false;
}
bool ReagentStore::run(const QString& sql, const QVariantList& values, QString* message)
{
    QSqlQuery query(database_);
    if (!query.prepare(sql))
    {
        *message = query.lastError().text();
        return false;
    }
    for (const QVariant& value : values)
        query.addBindValue(value);
    if (!query.exec())
    {
        *message = query.lastError().text();
        return false;
    }
    if (sql.startsWith("UPDATE ") && query.numRowsAffected() != 1)
    {
        *message = QStringLiteral("记录已不存在，请重新读取后编辑");
        return false;
    }
    return true;
}
void ReagentStore::initialize(QString directory)
{
    const QString operation = QStringLiteral("初始化试剂");
    if (!onWorker(operation))
        return;
    close();
    directory = QDir::cleanPath(directory);
    if (!QFileInfo(directory).isAbsolute() || directory.contains("sn100_host_reference", Qt::CaseInsensitive))
    {
        emit error(operation, QStringLiteral("须指定已由项目模块初始化的隔离绝对数据目录"));
        return;
    }
    for (QFileInfo current(directory); !current.filePath().isEmpty(); current.setFile(current.absolutePath()))
    {
        if (current.isSymLink())
        {
            emit error(operation, QStringLiteral("隔离目录不能经过符号链接"));
            return;
        }
        if (current.absoluteFilePath() == current.absolutePath())
            break;
    }
    QDir catalog(directory);
    const QFileInfo marker(catalog.filePath(".sn100-catalog-offline-v1"));
    const QFileInfo database(catalog.filePath("catalog.db"));
    if (!marker.isFile() || marker.isSymLink() || !database.isFile() || database.isSymLink())
    {
        emit error(operation, QStringLiteral("项目隔离库/标记不存在或为链接，先成功初始化项目模块"));
        return;
    }
    QFile markerFile(marker.absoluteFilePath());
    if (!markerFile.open(QIODevice::ReadOnly) || markerFile.read(128).trimmed() != "sn100-catalog-offline-v1")
    {
        emit error(operation, QStringLiteral("项目隔离标记内容不匹配"));
        return;
    }
    connectionName_ = QStringLiteral("reagents_%1").arg(QUuid::createUuid().toString());
    database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    database_.setDatabaseName(QDir(directory).filePath(QStringLiteral("catalog.db")));
    database_.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=800"));
    if (!database_.open())
    {
        emit error(operation, database_.lastError().text());
        close();
        return;
    }
    QString message;
    {
        QSqlQuery metadata(database_);
        if (!metadata.exec("SELECT value FROM catalog_metadata WHERE key='schema'") || !metadata.next()
            || metadata.value(0).toString() != "sn100-catalog-offline-v1")
        {
            emit error(operation, QStringLiteral("现有数据库不是本模块隔离结构，保留原文件"));
            metadata.finish();
            close();
            return;
        }
        if (!metadata.exec("PRAGMA foreign_keys=ON") || !metadata.exec("PRAGMA foreign_keys")
            || !metadata.next() || metadata.value(0).toInt() != 1)
        {
            emit error(operation, QStringLiteral("无法启用SQLite引用保护"));
            metadata.finish();
            close();
            return;
        }
    }
    // foreign_keys protects project deletion even when another page/connection
    // changes the catalog while an editor has an older value snapshot.
    const QStringList schema = { "PRAGMA busy_timeout=800",
        "CREATE TABLE IF NOT EXISTS reagent_list(id INTEGER PRIMARY KEY,reagent_name TEXT NOT NULL "
        "UNIQUE,reagent_type TEXT NOT NULL,manufacturer TEXT NOT NULL DEFAULT '',warning_ul INTEGER NOT "
        "NULL,out_ul INTEGER NOT NULL,valid INTEGER NOT NULL,check_limit INTEGER NOT NULL,fixed INTEGER NOT "
        "NULL DEFAULT 0)",
        "CREATE TABLE IF NOT EXISTS project_action_reagent_reference(project_id INTEGER NOT NULL REFERENCES "
        "project_base(id) ON DELETE CASCADE,reagent_id INTEGER NOT NULL REFERENCES reagent_list(id) ON DELETE "
        "RESTRICT,PRIMARY KEY(project_id,reagent_id))",
        "CREATE TABLE IF NOT EXISTS reagent_bottle_type(id INTEGER PRIMARY KEY,bottle_type_name TEXT NOT "
        "NULL,area TEXT NOT NULL,max_amount_ul INTEGER NOT NULL,bottle_real_height_mm REAL NOT NULL DEFAULT "
        "0,UNIQUE(bottle_type_name,area))",
        "CREATE TABLE IF NOT EXISTS reagent_batch_group(id INTEGER PRIMARY KEY,project_id INTEGER NOT NULL "
        "REFERENCES project_base(id) ON DELETE RESTRICT,batch_code TEXT NOT NULL,batch_group TEXT NOT "
        "NULL,UNIQUE(project_id,batch_code))",
        "CREATE TABLE IF NOT EXISTS reagent_batch_metadata(batch_id INTEGER PRIMARY KEY REFERENCES "
        "reagent_batch_group(id) ON DELETE CASCADE,isi_value TEXT NOT NULL,normal_value TEXT NOT NULL)",
        "CREATE TABLE IF NOT EXISTS catalog_batch_reference(owner_module TEXT NOT NULL,reference_key "
        "TEXT NOT NULL,batch_id INTEGER NOT NULL REFERENCES reagent_batch_group(id) ON DELETE RESTRICT,"
        "PRIMARY KEY(owner_module,reference_key))",
        "CREATE INDEX IF NOT EXISTS catalog_batch_reference_batch ON catalog_batch_reference(batch_id)",
        "INSERT OR IGNORE INTO catalog_metadata(key,value) VALUES('reagent_batch_metadata','v1')",
        "CREATE TABLE IF NOT EXISTS reagent_barcode(id INTEGER PRIMARY KEY,reagent_id INTEGER NOT NULL "
        "REFERENCES reagent_list(id) ON DELETE RESTRICT,bottle_id INTEGER NOT NULL REFERENCES "
        "reagent_bottle_type(id) ON DELETE RESTRICT,reagent_barcode TEXT NOT NULL UNIQUE,out_date TEXT NOT "
        "NULL)",
        "CREATE TABLE IF NOT EXISTS reagent_setting(id INTEGER PRIMARY KEY,hole_name TEXT NOT NULL "
        "UNIQUE,area TEXT NOT NULL,reagent_id INTEGER REFERENCES reagent_list(id) ON DELETE "
        "RESTRICT,bottle_id INTEGER REFERENCES reagent_bottle_type(id) ON DELETE RESTRICT,batch_id INTEGER "
        "REFERENCES reagent_batch_group(id) ON DELETE RESTRICT,out_date TEXT NOT NULL DEFAULT '',amount_ul "
        "INTEGER NOT NULL DEFAULT 0,used_ul INTEGER NOT NULL DEFAULT 0,priority INTEGER NOT NULL DEFAULT "
        "99,datetime TEXT NOT NULL DEFAULT '')" };
    if (!database_.transaction())
    {
        emit error(operation, database_.lastError().text());
        close();
        return;
    }
    // Check the extension version inside this schema transaction. An unknown
    // marker is preserved, never overwritten by INSERT OR IGNORE below.
    {
        QSqlQuery version(database_);
        const bool read = version.exec("SELECT value FROM catalog_metadata WHERE key='reagent_batch_metadata'");
        if (!read || (version.next() && version.value(0).toString() != "v1"))
        {
            const QString failure = read ? QStringLiteral("试剂批组扩展版本不支持，保留现有数据库") : version.lastError().text();
            version.finish();
            database_.rollback();
            emit error(operation, failure);
            close();
            return;
        }
    }
    bool success = true;
    for (const QString& sql : schema)
        if (!(success = run(sql, {}, &message)))
            break;
    if (success)
        success = database_.commit();
    else
        database_.rollback();
    if (!success)
    {
        database_.rollback();
        emit error(operation, message.isEmpty() ? database_.lastError().text() : message);
        close();
        return;
    }
    ready_ = true;
    emit ready();
    load();
}
void ReagentStore::setSession(AccountSession session)
{
    if (onWorker(QStringLiteral("试剂权限")))
        session_ = session;
}
QString ReagentStore::validateBatchMetadata(QString isiValue, QString normalValue)
{
    // MaskDlgGroupSetting requests min=0/max=9999/decimalCount=4 from
    // SystemCommon::lineEditValidator. Keep decimal text, never compute INR.
    const QRegularExpression decimal(QStringLiteral("^(0|[1-9][0-9]{0,3})(\\.[0-9]{1,4})?$"));
    for (const QString& value : {isiValue, normalValue})
        if (value != value.trimmed() || !decimal.match(value).hasMatch() || value.toDouble() > 9999.0)
            return QStringLiteral("ISI值和正常测定值须同时填写，范围0–9999、最多4位小数；仅保存本地资料");
    return QString();
}
bool ReagentStore::referenceReady(qint64 requestId, const QString& operation, bool write)
{
    QString message;
    if (QThread::currentThread() != thread())
        message = QStringLiteral("试剂存储线程错误");
    else if (!ready_ || !database_.isOpen())
        message = QStringLiteral("试剂数据库未就绪");
    else if (write && (!session_.authenticated || !session_.actionReady || session_.role < 1 || session_.role > 5))
        message = QStringLiteral("当前账户不可修改批组引用");
    if (message.isEmpty())
        return true;
    emit batchReferenceError(requestId, operation, message);
    return false;
}
void ReagentStore::holdBatchReference(qint64 requestId, QString module, QString key, qint64 batchId)
{
    const QString operation = QStringLiteral("保留批组引用");
    if (!referenceReady(requestId, operation, true))
        return;
    if (!QRegularExpression("^[a-z][a-z0-9_]{0,31}\\z").match(module).hasMatch()
        || key.isEmpty() || key != key.trimmed() || key.size() > 128 || batchId <= 0)
    {
        emit batchReferenceError(requestId, operation, QStringLiteral("模块名、引用键或批组编号不合法"));
        return;
    }
    if (!database_.transaction())
    {
        emit batchReferenceError(requestId, operation, database_.lastError().text());
        return;
    }
    QString message;
    QSqlQuery query(database_);
    query.prepare("SELECT batch_id FROM catalog_batch_reference WHERE owner_module=? AND reference_key=?");
    query.addBindValue(module);
    query.addBindValue(key);
    bool success = query.exec();
    if (!success)
        message = query.lastError().text();
    else if (query.next())
    {
        success = query.value(0).toLongLong() == batchId;
        if (!success)
            message = QStringLiteral("该引用键已保留另一批组，须显式解除旧引用后再绑定");
    }
    else
        success = run("INSERT INTO catalog_batch_reference(owner_module,reference_key,batch_id) VALUES(?,?,?)",
            {module, key, batchId}, &message);
    query.finish();
    if (success)
    {
        success = database_.commit();
        if (!success)
            message = database_.lastError().text();
    }
    if (!success)
    {
        database_.rollback();
        emit batchReferenceError(requestId, operation, message);
        return;
    }
    emit batchReferenceSucceeded(requestId, operation);
}
void ReagentStore::releaseBatchReference(qint64 requestId, QString module, QString key)
{
    const QString operation = QStringLiteral("解除批组引用");
    if (!referenceReady(requestId, operation, true))
        return;
    if (!QRegularExpression("^[a-z][a-z0-9_]{0,31}\\z").match(module).hasMatch()
        || key.isEmpty() || key != key.trimmed() || key.size() > 128)
    {
        emit batchReferenceError(requestId, operation, QStringLiteral("模块名或引用键不合法"));
        return;
    }
    // A single committed catalog statement; no external record is removed here.
    QString message;
    if (!run("DELETE FROM catalog_batch_reference WHERE owner_module=? AND reference_key=?", {module, key}, &message))
    {
        emit batchReferenceError(requestId, operation, message);
        return;
    }
    emit batchReferenceSucceeded(requestId, operation);
}
void ReagentStore::checkBatchReferences(qint64 requestId, qint64 batchId)
{
    const QString operation = QStringLiteral("检查批组引用");
    if (!referenceReady(requestId, operation, false))
        return;
    if (!database_.transaction())
    {
        emit batchReferenceError(requestId, operation, database_.lastError().text());
        return;
    }
    QSqlQuery query(database_);
    query.prepare("SELECT id FROM reagent_batch_group WHERE id=?");
    query.addBindValue(batchId);
    bool success = query.exec();
    const bool exists = success && query.next();
    QString message = query.lastError().text();
    ReagentBatchReferences references;
    if (success)
    {
        query.prepare("SELECT owner_module,reference_key,batch_id FROM catalog_batch_reference WHERE batch_id=? ORDER BY owner_module,reference_key");
        query.addBindValue(batchId);
        success = query.exec();
        message = query.lastError().text();
        while (success && query.next())
            references.append({query.value(0).toString(), query.value(1).toString(), query.value(2).toLongLong()});
    }
    query.finish();
    if (success)
    {
        success = database_.commit();
        if (!success)
            message = database_.lastError().text();
    }
    if (!success)
    {
        database_.rollback();
        emit batchReferenceError(requestId, operation, message);
        return;
    }
    emit batchReferencesReady(requestId, batchId, exists, references);
}
bool ReagentStore::begin(const QString& operation)
{
    if (!onWorker(operation))
        return false;
    if (!ready_ || !database_.isOpen())
    {
        emit error(operation, QStringLiteral("试剂数据库未就绪"));
        return false;
    }
    if (!session_.authenticated || !session_.actionReady || session_.role < 1 || session_.role > 5)
    {
        emit error(operation, QStringLiteral("当前账户不可编辑试剂；请选择允许操作的离线身份"));
        return false;
    }
    if (!database_.transaction())
    {
        emit error(operation, database_.lastError().text());
        return false;
    }
    return true;
}
bool ReagentStore::finish(const QString& operation, bool success, const QString& message)
{
    QString failure = message;
    if (success && !database_.commit())
    {
        success = false;
        failure = database_.lastError().text();
    }
    if (!success)
    {
        database_.rollback();
        emit error(operation,
            failure.contains("FOREIGN KEY")
                ? QStringLiteral("该资料仍被孔位、条码、批号或项目引用，不能删除；先解除引用")
                : failure);
        return false;
    }
    emit succeeded(operation);
    load();
    return true;
}
void ReagentStore::load()
{
    if (originalEditor_) { loadOriginal(); return; }
    const QString operation = QStringLiteral("读取试剂");
    if (!onWorker(operation))
        return;
    if (!ready_ || !database_.isOpen())
    {
        emit error(operation, QStringLiteral("试剂数据库未就绪"));
        return;
    }
    ReagentSnapshot snapshot;
    if (!database_.transaction())
    {
        emit error(operation, database_.lastError().text());
        return;
    }
    QSqlQuery q(database_);
    auto query = [&](const QString& sql)
    {
        if (q.exec(sql))
            return true;
        database_.rollback();
        emit error(operation, q.lastError().text());
        return false;
    };
    if (!query("SELECT id,reagent_name,reagent_type,manufacturer,warning_ul,out_ul,valid,check_limit,fixed "
               "FROM reagent_list ORDER BY id"))
        return;
    while (q.next())
    {
        ReagentRecord r;
        r.id = q.value(0).toLongLong();
        r.name = q.value(1).toString();
        r.type = q.value(2).toString();
        r.manufacturer = q.value(3).toString();
        r.warningUl = q.value(4).toInt();
        r.outUl = q.value(5).toInt();
        r.valid = q.value(6).toBool();
        r.checkExpiry = q.value(7).toBool();
        r.fixed = q.value(8).toBool();
        snapshot.reagents.append(r);
    }
    if (!query("SELECT id,bottle_type_name,area,max_amount_ul,bottle_real_height_mm FROM reagent_bottle_type "
               "ORDER BY id"))
        return;
    while (q.next())
    {
        ReagentBottle b;
        b.id = q.value(0).toLongLong();
        b.name = q.value(1).toString();
        b.area = q.value(2).toString();
        b.capacityUl = q.value(3).toInt();
        b.heightMm = q.value(4).toDouble();
        snapshot.bottles.append(b);
    }
    if (!query("SELECT b.id,b.project_id,b.batch_code,b.batch_group,m.batch_id,m.isi_value,m.normal_value "
               "FROM reagent_batch_group b LEFT JOIN reagent_batch_metadata m ON m.batch_id=b.id ORDER BY b.id"))
        return;
    while (q.next())
    {
        ReagentBatch b;
        b.id = q.value(0).toLongLong();
        b.projectId = q.value(1).toLongLong();
        b.code = q.value(2).toString();
        b.batchGroup = q.value(3).toString();
        b.hasMetadata = !q.value(4).isNull();
        b.isiValue = q.value(5).toString();
        b.normalValue = q.value(6).toString();
        if (b.hasMetadata && !validateBatchMetadata(b.isiValue, b.normalValue).isEmpty())
        {
            database_.rollback();
            emit error(operation, QStringLiteral("批组%1的ISI/正常测定值资料损坏，保留原数据，请核查后恢复").arg(b.id));
            return;
        }
        snapshot.batches.append(b);
    }
    if (!query("SELECT id,reagent_id,bottle_id,reagent_barcode,out_date FROM reagent_barcode ORDER BY id"))
        return;
    while (q.next())
    {
        ReagentBarcode b;
        b.id = q.value(0).toLongLong();
        b.reagentId = q.value(1).toLongLong();
        b.bottleId = q.value(2).toLongLong();
        b.code = q.value(3).toString();
        b.expiry = q.value(4).toString();
        snapshot.barcodes.append(b);
    }
    if (!query("SELECT "
               "id,hole_name,area,reagent_id,bottle_id,batch_id,out_date,amount_ul,used_ul,priority,datetime "
               "FROM reagent_setting ORDER BY id"))
        return;
    while (q.next())
    {
        ReagentHole h;
        h.id = q.value(0).toLongLong();
        h.name = q.value(1).toString();
        h.area = q.value(2).toString();
        h.reagentId = q.value(3).toLongLong();
        h.bottleId = q.value(4).toLongLong();
        h.batchId = q.value(5).toLongLong();
        h.expiry = q.value(6).toString();
        h.amountUl = q.value(7).toInt();
        h.remainingUl = q.value(8).toInt();
        h.priority = q.value(9).toInt();
        h.changedAt = q.value(10).toString();
        snapshot.holes.append(h);
    }
    if (!database_.commit())
    {
        const QString failure = database_.lastError().text();
        database_.rollback();
        emit error(operation, failure);
        return;
    }
    emit loaded(snapshot);
}
void ReagentStore::saveReagent(ReagentRecord r)
{
    if (originalEditor_)
    {
        const QString operation=QStringLiteral("保存试剂资料");
        if (!originalMayEdit(operation)) return;
        QString message; const bool success=originalEditor_->saveReagent(r,&message);
        finishOriginal(operation,success,message); return;
    }
    const QString op = QStringLiteral("保存试剂资料");
    if (!begin(op))
        return;
    const QStringList types = { "无", "试剂", "缓冲稀释液", "清洗液", "质控品", "校准品", "因子血浆" };
    if (r.name.trimmed().isEmpty() || r.name != r.name.trimmed() || r.name.size() > 16
        || !types.contains(r.type) || r.warningUl < 0 || r.warningUl > 9999 || r.outUl < 0
        || r.outUl > r.warningUl)
    {
        finish(op, false, QStringLiteral("名称须为1–16字且无首尾空格；试剂量须满足0≤缺少量≤警告量≤9999 µL"));
        return;
    }
    QString error;
    if (r.id)
    {
        QSqlQuery actionReference(database_);
        actionReference.prepare("SELECT r.reagent_name,r.reagent_type FROM reagent_list r WHERE r.id=? "
                                "AND EXISTS(SELECT 1 FROM project_action_reagent_reference a WHERE a.reagent_id=r.id)");
        actionReference.addBindValue(r.id);
        if (!actionReference.exec())
        {
            finish(op, false, actionReference.lastError().text());
            return;
        }
        if (actionReference.next()
            && (actionReference.value(0).toString() != r.name || actionReference.value(1).toString() != r.type || !r.valid))
        {
            finish(op, false, QStringLiteral("试剂仍被项目动作参数引用，不能改名、改类型或停用；先修改项目方案解除引用"));
            return;
        }
        QSqlQuery references(database_);
        references.prepare("SELECT area FROM reagent_setting WHERE reagent_id=?");
        references.addBindValue(r.id);
        if (!references.exec())
        {
            finish(op, false, references.lastError().text());
            return;
        }
        while (references.next())
        {
            const QString area = references.value(0).toString();
            if (!r.valid || (area == "r_area" && r.type != "试剂" && r.type != "清洗液")
                || (area == "s_area" && r.type != "缓冲稀释液" && r.type != "清洗液"))
            {
                finish(
                    op, false, QStringLiteral("试剂仍被孔位引用，不能改成不适用类型或停用；先解除孔位配置"));
                return;
            }
        }
    }
    if (r.manufacturer.isNull())
        r.manufacturer = QStringLiteral("");
    QVariantList values = { r.name, r.type, r.manufacturer, r.warningUl, r.outUl, r.valid, r.checkExpiry };
    QString sql = "INSERT INTO "
                  "reagent_list(reagent_name,reagent_type,manufacturer,warning_ul,out_ul,valid,check_limit) "
                  "VALUES(?,?,?,?,?,?,?)";
    if (r.id)
    {
        sql = "UPDATE reagent_list SET "
              "reagent_name=?,reagent_type=?,manufacturer=?,warning_ul=?,out_ul=?,valid=?,check_limit=? "
              "WHERE id=?";
        values << r.id;
    }
    const bool saved = run(sql, values, &error);
    finish(op, saved, error);
}
bool ReagentStore::remove(const QString& table, qint64 id, const QString& op, const QString& extra)
{
    if (originalEditor_)
    {
        if (!originalMayEdit(op)) return false;
        const QString kind=table=="reagent_list" ? "reagent" : table=="reagent_bottle_type" ? "bottle" : table=="reagent_batch_group" ? "batch" : "barcode";
        QString message; const bool success=originalEditor_->remove(kind,id,&message);
        finishOriginal(op,success,message); return success;
    }
    if (!begin(op))
        return false;
    QString message;
    QSqlQuery query(database_);
    query.prepare("DELETE FROM " + table + " WHERE id=?" + extra);
    query.addBindValue(id);
    const bool ok = query.exec();
    message = query.lastError().text();
    if (ok && query.numRowsAffected() != 1)
        return finish(op, false, QStringLiteral("未删除：记录不存在或为不可删除的内置资料"));
    return finish(op, ok, message);
}
void ReagentStore::removeReagent(qint64 id)
{
    remove("reagent_list", id, QStringLiteral("删除试剂资料"), " AND fixed=0");
}
void ReagentStore::removeBottle(qint64 id)
{
    remove("reagent_bottle_type", id, QStringLiteral("删除瓶型"));
}
void ReagentStore::removeBatch(qint64 id)
{
    remove("reagent_batch_group", id, QStringLiteral("删除试剂批号"));
}
void ReagentStore::removeBarcode(qint64 id)
{
    remove("reagent_barcode", id, QStringLiteral("删除条码"));
}
void ReagentStore::saveBottle(ReagentBottle b)
{
    if (originalEditor_)
    {
        const QString operation=QStringLiteral("保存瓶型");
        if (!originalMayEdit(operation)) return;
        QString message; const bool success=originalEditor_->saveBottle(b,&message);
        finishOriginal(operation,success,message); return;
    }
    const QString op = QStringLiteral("保存瓶型");
    if (!begin(op))
        return;
    if (b.name.trimmed().isEmpty() || b.name.size() > 32 || (b.area != "r_area" && b.area != "s_area")
        || b.capacityUl <= 0 || b.capacityUl > 100000 || b.heightMm < 0 || b.heightMm > 500)
    {
        finish(op, false, QStringLiteral("瓶型名称/区域/容量不合法；容量范围1–100000 µL，高度0–500 mm"));
        return;
    }
    QString error;
    QVariantList values = { b.name, b.area, b.capacityUl, b.heightMm };
    QString sql
        = "INSERT INTO reagent_bottle_type(bottle_type_name,area,max_amount_ul,bottle_real_height_mm) "
          "VALUES(?,?,?,?)";
    if (b.id)
    {
        QSqlQuery existing(database_);
        existing.prepare("SELECT COUNT(*) FROM reagent_setting WHERE bottle_id=?");
        existing.addBindValue(b.id);
        if (!existing.exec() || !existing.next())
        {
            finish(op, false, existing.lastError().text());
            return;
        }
        if (existing.value(0).toInt())
        {
            finish(op, false, QStringLiteral("瓶型已配置到孔位，请先解除孔位配置再修改"));
            return;
        }
        sql = "UPDATE reagent_bottle_type SET "
              "bottle_type_name=?,area=?,max_amount_ul=?,bottle_real_height_mm=? WHERE id=?";
        values << b.id;
    }
    const bool saved = run(sql, values, &error);
    finish(op, saved, error);
}
void ReagentStore::saveBatch(ReagentBatch b)
{
    if (originalEditor_)
    {
        const QString operation=QStringLiteral("保存试剂批号");
        if (!originalMayEdit(operation)) return;
        QString message; const bool success=originalEditor_->saveBatch(b,&message);
        finishOriginal(operation,success,message); return;
    }
    const QString op = QStringLiteral("保存试剂批号");
    if (!begin(op))
        return;
    if (b.projectId <= 0 || b.code.trimmed().isEmpty() || b.code != b.code.trimmed() || b.code.size() > 32
        || b.batchGroup.trimmed().isEmpty() || b.batchGroup.size() > 256)
    {
        finish(op, false, QStringLiteral("先选项目，批号须为1–32字，批号组不能为空"));
        return;
    }
    const QStringList members = b.batchGroup.split('|', Qt::KeepEmptyParts);
    QSet<QString> uniqueMembers;
    for (const QString& member : members)
    {
        if (member.isEmpty() || member != member.trimmed() || member.size() > 32
            || uniqueMembers.contains(member))
        {
            finish(op, false, QStringLiteral("批号组每项须为1–32字、无首尾空格，不能有空项或重复批号"));
            return;
        }
        uniqueMembers.insert(member);
    }
    if (b.metadataEdited)
    {
        const QString validation = validateBatchMetadata(b.isiValue, b.normalValue);
        if (!validation.isEmpty())
        {
            finish(op, false, validation);
            return;
        }
    }
    QSqlQuery project(database_);
    project.prepare("SELECT is_used FROM project_base WHERE id=?");
    project.addBindValue(b.projectId);
    if (!project.exec())
    {
        finish(op, false, project.lastError().text());
        return;
    }
    if (!project.next() || !project.value(0).toBool())
    {
        finish(op, false, QStringLiteral("项目已不存在或已停用，不能保存新批号；请重新选择有效项目"));
        return;
    }
    QSqlQuery otherGroups(database_);
    otherGroups.prepare("SELECT batch_group FROM reagent_batch_group WHERE project_id=? AND id<>?");
    otherGroups.addBindValue(b.projectId);
    otherGroups.addBindValue(b.id);
    if (!otherGroups.exec())
    {
        finish(op, false, otherGroups.lastError().text());
        return;
    }
    while (otherGroups.next())
        for (const QString& member : otherGroups.value(0).toString().split('|', Qt::KeepEmptyParts))
            if (uniqueMembers.contains(member))
            {
                finish(op, false, QStringLiteral("同一项目的该试剂批号已在其他组中，不能重复分组"));
                return;
            }
    QString error;
    QVariantList values = { b.projectId, b.code, b.batchGroup };
    QString sql = "INSERT INTO reagent_batch_group(project_id,batch_code,batch_group) VALUES(?,?,?)";
    if (b.id)
    {
        QSqlQuery existing(database_);
        existing.prepare("SELECT b.project_id,b.batch_code,b.batch_group,(SELECT COUNT(*) FROM "
                         "catalog_batch_reference r WHERE r.batch_id=b.id) FROM reagent_batch_group b WHERE b.id=?");
        existing.addBindValue(b.id);
        if (!existing.exec() || !existing.next())
        {
            finish(op, false, QStringLiteral("批组已不存在，请重新读取"));
            return;
        }
        if (existing.value(3).toInt() > 0
            && (existing.value(0).toLongLong() != b.projectId || existing.value(1).toString() != b.code
                || existing.value(2).toString() != b.batchGroup))
        {
            finish(op, false, QStringLiteral("批组被其他模块保留引用，不能改名、改组或换项目；先解除引用"));
            return;
        }
        sql = "UPDATE reagent_batch_group SET project_id=?,batch_code=?,batch_group=? WHERE id=?";
        values << b.id;
    }
    bool saved = run(sql, values, &error);
    if (saved && b.metadataEdited)
    {
        qint64 batchId = b.id;
        if (!batchId)
        {
            QSqlQuery inserted(database_);
            saved = inserted.exec("SELECT last_insert_rowid()") && inserted.next();
            if (saved)
                batchId = inserted.value(0).toLongLong();
            else
                error = inserted.lastError().text();
        }
        if (saved)
            saved = run("INSERT INTO reagent_batch_metadata(batch_id,isi_value,normal_value) VALUES(?,?,?) "
                        "ON CONFLICT(batch_id) DO UPDATE SET isi_value=excluded.isi_value,normal_value=excluded.normal_value",
                {batchId, b.isiValue, b.normalValue}, &error);
    }
    finish(op, saved, error);
}
void ReagentStore::saveBarcode(ReagentBarcode b)
{
    if (originalEditor_)
    {
        const QString operation=QStringLiteral("保存条码");
        if (!originalMayEdit(operation)) return;
        QString message; const bool success=originalEditor_->saveBarcode(b,&message);
        finishOriginal(operation,success,message); return;
    }
    const QString op = QStringLiteral("保存试剂条码");
    if (!begin(op))
        return;
    if (b.reagentId <= 0 || b.bottleId <= 0 || b.code.trimmed().isEmpty() || b.code.size() > 64
        || !QDate::fromString(b.expiry, "yyyy-MM-dd").isValid())
    {
        finish(op, false, QStringLiteral("条码、瓶型与有效日期不合法"));
        return;
    }
    QString error;
    QVariantList values = { b.reagentId, b.bottleId, b.code, b.expiry };
    QString sql
        = "INSERT INTO reagent_barcode(reagent_id,bottle_id,reagent_barcode,out_date) VALUES(?,?,?,?)";
    if (b.id)
    {
        sql = "UPDATE reagent_barcode SET reagent_id=?,bottle_id=?,reagent_barcode=?,out_date=? WHERE id=?";
        values << b.id;
    }
    const bool saved = run(sql, values, &error);
    finish(op, saved, error);
}
void ReagentStore::saveHole(ReagentHole h)
{
    if (originalEditor_)
    {
        const QString operation=QStringLiteral("保存孔位");
        if (!originalMayEdit(operation)) return;
        QString message; const bool success=originalEditor_->saveHole(h,&message);
        finishOriginal(operation,success,message); return;
    }
    const QString op = QStringLiteral("保存孔位配置");
    if (!begin(op))
        return;
    const QRegularExpression holeNames("^(R_([1-9]|1[0-9]|20)|SD_[1-4])$");
    if (!holeNames.match(h.name).hasMatch() || h.area != (h.name.startsWith("R_") ? "r_area" : "s_area")
        || h.reagentId <= 0 || h.bottleId <= 0 || !QDate::fromString(h.expiry, "yyyy-MM-dd").isValid())
    {
        finish(op, false, QStringLiteral("孔位、区域、资料、瓶型或有效日期不合法"));
        return;
    }
    QSqlQuery q(database_);
    q.prepare("SELECT b.max_amount_ul,b.area,r.reagent_type,r.valid FROM reagent_bottle_type b,reagent_list "
              "r WHERE b.id=? AND r.id=?");
    q.addBindValue(h.bottleId);
    q.addBindValue(h.reagentId);
    if (!q.exec() || !q.next())
    {
        finish(op, false, QStringLiteral("找不到瓶型或试剂资料"));
        return;
    }
    const QString type = q.value(2).toString();
    if (q.value(1).toString() != h.area || !q.value(3).toBool()
        || (h.area == "r_area" && type != "试剂" && type != "清洗液")
        || (h.area == "s_area" && type != "缓冲稀释液" && type != "清洗液"))
    {
        finish(op, false, QStringLiteral("瓶型/试剂类型与孔位区域不匹配，或试剂无效"));
        return;
    }
    if (h.amountUl < 0 || h.amountUl > q.value(0).toInt() || h.remainingUl < 0 || h.remainingUl > h.amountUl)
    {
        finish(op, false, QStringLiteral("本地容量须满足0≤余量≤装载量≤瓶型上限，单位µL"));
        return;
    }
    if (h.batchId)
    {
        QSqlQuery project(database_);
        project.prepare("SELECT p.is_used FROM reagent_batch_group b JOIN project_base p ON "
                        "p.id=b.project_id WHERE b.id=?");
        project.addBindValue(h.batchId);
        if (!project.exec() || !project.next() || !project.value(0).toBool())
        {
            finish(op, false, QStringLiteral("批号项目已不存在或已停用，不能保存孔位配置"));
            return;
        }
    }
    QString error;
    const QVariant batch = h.batchId ? QVariant(h.batchId) : QVariant(QVariant::LongLong);
    h.changedAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    // This writes a local configuration only. No instrument TX or measured
    // liquid level is fabricated; remainingUl is explicitly entered locally.
    const bool saved
        = run("INSERT INTO "
              "reagent_setting(hole_name,area,reagent_id,bottle_id,batch_id,out_date,amount_ul,used_ul,"
              "priority,datetime) VALUES(?,?,?,?,?,?,?,?,?,?) ON CONFLICT(hole_name) DO UPDATE SET "
              "area=excluded.area,reagent_id=excluded.reagent_id,bottle_id=excluded.bottle_id,batch_id="
              "excluded.batch_id,out_date=excluded.out_date,amount_ul=excluded.amount_ul,used_ul=excluded."
              "used_ul,priority=excluded.priority,datetime=excluded.datetime",
            { h.name, h.area, h.reagentId, h.bottleId, batch, h.expiry, h.amountUl, h.remainingUl, h.priority,
                h.changedAt },
            &error);
    finish(op, saved, error);
}
void ReagentStore::clearHole(QString name)
{
    if (originalEditor_)
    {
        const QString operation=QStringLiteral("清除孔位配置");
        if (!originalMayEdit(operation)) return;
        QString message; const bool success=originalEditor_->clearHole(name,&message);
        finishOriginal(operation,success,message); return;
    }
    const QString op = QStringLiteral("清除本地孔位配置");
    if (!begin(op))
        return;
    QString error;
    const bool saved = run("DELETE FROM reagent_setting WHERE hole_name=?", { name }, &error);
    finish(op, saved, error);
}
void ReagentStore::close()
{
    if (originalEditor_)
    {
        if (!onWorker(QStringLiteral("关闭原试剂"))) return;
        originalEditor_.reset(); ready_=false; emit closed();
    }
    ready_ = false;
    if (connectionName_.isEmpty())
        return;
    if (!onWorker(QStringLiteral("关闭试剂")))
        return;
    database_.close();
    database_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName_);
    connectionName_.clear();
    emit closed();
}
