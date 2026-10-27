// 单孔质控样本的持久引用保护；按db4610b5共同合同及943e2d14字段澄清实现。
// 所有SQL位于QcStore所属线程；两库始终按QC->catalog取锁，各自提交。
#include "qc_store.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>
#include <limits>

namespace
{
const QString sampleReferenceSchema = QStringLiteral("sn100-qc-sample-reference-v1");
bool sampleReject(QString *message, const QString &text)
{
    *message = text;
    return false;
}
bool sampleRun(QSqlQuery *query, const QString &sql, const QVariantList &values, QString *message)
{
    if (!query->prepare(sql)) return sampleReject(message, query->lastError().text());
    for (const QVariant &value : values) query->addBindValue(value);
    if (!query->exec()) return sampleReject(message, query->lastError().text());
    return true;
}
bool sampleFetched(const QSqlQuery &query, QString *message)
{
    return !query.lastError().isValid() || sampleReject(message, query.lastError().text());
}
bool sampleUuid(const QString &text)
{
    return !QUuid(text).isNull() && text == text.trimmed() && !text.contains(QChar::Null);
}
bool sampleMetadata(QSqlDatabase &db, const QString &table, const QString &key,
                    QString *value, QString *message)
{
    QSqlQuery query(db);
    // table仅为本文件两个固定表名，不采用业务输入作SQL标识符。
    if (!sampleRun(&query, "SELECT value FROM " + table + " WHERE key=?", {key}, message)) return false;
    const bool found = query.next();
    if (!sampleFetched(query, message)) return false;
    if (!found) return sampleReject(message, QStringLiteral("数据集元数据缺失：%1；未自动重建身份。").arg(key));
    *value = query.value(0).toString();
    return true;
}
bool sampleRevision(QSqlDatabase &catalog, qint64 *revision, QString *message)
{
    QString value;
    if (!sampleMetadata(catalog, "catalog_metadata", "sample_qc_revision", &value, message)) return false;
    bool valid = false;
    const qint64 number = value.toLongLong(&valid);
    if (!valid || number < 0 || QString::number(number) != value)
        return sampleReject(message, QStringLiteral("样本质控修订号无效，保留资料并核对恢复来源。"));
    *revision = number;
    return true;
}
struct SampleSourceOutcome
{
    bool exists = false;
    QString owner, quality, outcome;
    qint64 expected = 0;
    QVariant finalRevision;
};
bool sampleOutcome(QSqlDatabase &catalog, const QString &token, SampleSourceOutcome *outcome,
                   QString *message)
{
    QSqlQuery query(catalog);
    if (!sampleRun(&query, "SELECT owner_uuid,quality_uuid,before_revision,outcome,after_revision,operation,target_rack_id "
                   "FROM sample_local_quality_change WHERE change_token=?", {token}, message)) return false;
    SampleSourceOutcome found;
    found.exists = query.next();
    if (!sampleFetched(query, message)) return false;
    if (!found.exists) { *outcome = found; return true; }
    found.owner = query.value(0).toString(); found.quality = query.value(1).toString();
    bool valid = false;
    found.expected = query.value(2).toLongLong(&valid);
    found.outcome = query.value(3).toString(); found.finalRevision = query.value(4);
    if (!valid || found.expected < 0 || !sampleUuid(found.owner) || !sampleUuid(found.quality) ||
        (found.outcome != "pending" && found.outcome != "committed" && found.outcome != "aborted"))
        return sampleReject(message, QStringLiteral("样本持久结局字段损坏，禁止推断是否已保存。"));
    const QString operation = query.value(5).toString();
    bool targetValid = false;
    const qint64 target = query.value(6).toLongLong(&targetValid);
    if (!targetValid || target < 0 || (operation != "saveRack" && operation != "removeRack" && operation != "clearOrdinaryRacks") ||
        (operation == "removeRack" && target == 0))
        return sampleReject(message, QStringLiteral("源变更操作或目标架身份损坏，保留保护。"));
    if (found.outcome != "committed" && !found.finalRevision.isNull())
        return sampleReject(message, QStringLiteral("待定/取消结局带有最终修订号，需核对恢复来源。"));
    if (!found.finalRevision.isNull())
    {
        bool finalValid = false;
        const qint64 final = found.finalRevision.toLongLong(&finalValid);
        if (!finalValid || final < 0 || (found.outcome == "committed" &&
            (found.expected == std::numeric_limits<qint64>::max() || final != found.expected + 1)))
            return sampleReject(message, QStringLiteral("样本最终修订号与结局不一致。"));
    }
    else if (found.outcome == "committed")
        return sampleReject(message, QStringLiteral("已提交结局缺最终修订号，保留预约。"));
    *outcome = found;
    return true;
}
QString sampleClaimsDigest(const SampleQcChange &change)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_12);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << QStringLiteral("sn100-qc-claim-v1") << change.changeToken << change.ownerDatasetId
           << change.qualityDatasetId << change.catalogCanonicalPath << change.expectedRevision
           << quint32(change.claims.size());
    for (const SampleQcClaim &claim : change.claims)
    {
        const auto &selected = claim.selected;
        stream << claim.slotToken << selected.datasetId << selected.controlId << selected.batchId
               << selected.controlName << selected.batchName << selected.sampleId << quint32(selected.projects.size());
        for (const auto &project : selected.projects) stream << project.associationId << project.projectId;
    }
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
}

bool QcStore::initializeSampleReferences(QString *message)
{
    QSqlQuery marker(database_);
    if (!sampleRun(&marker, "SELECT value FROM qc_metadata WHERE key='sample_reference_schema'", {}, message)) return false;
    const bool marked = marker.next();
    if (!sampleFetched(marker, message)) return false;
    if (marked && marker.value(0).toString() != sampleReferenceSchema)
        return sampleReject(message, QStringLiteral("质控样本保护结构版本不支持，保留原文件。"));
    marker.finish();
    if (!sampleRun(&marker, "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name IN"
                   "('qc_sample_owner','qc_sample_change','qc_sample_hold','qc_sample_hold_project')", {}, message) || !marker.next()) return false;
    if (marker.value(0).toInt() != (marked ? 4 : 0))
        return sampleReject(message, QStringLiteral("质控样本保护表与版本不一致，禁止自动重建。"));
    marker.finish();
    if (!sampleRun(&marker, "SELECT COUNT(*) FROM sqlite_master WHERE type='trigger' AND name IN"
                   "('qc_sample_control_guard','qc_sample_batch_guard','qc_sample_association_guard')", {}, message) || !marker.next()) return false;
    if (marker.value(0).toInt() != (marked ? 3 : 0))
        return sampleReject(message, QStringLiteral("质控身份保护触发器与版本不一致，需核对结构。"));
    if (!marked)
    {
        const QStringList statements = {
            "CREATE TABLE qc_sample_owner(owner_dataset_uuid TEXT PRIMARY KEY,catalog_path TEXT NOT NULL UNIQUE)",
            "CREATE TABLE qc_sample_change(change_token TEXT PRIMARY KEY,owner_dataset_uuid TEXT NOT NULL REFERENCES "
            "qc_sample_owner(owner_dataset_uuid) ON DELETE RESTRICT,quality_dataset_uuid TEXT NOT NULL,expected_revision INTEGER "
            "NOT NULL CHECK(expected_revision>=0),claims_digest TEXT NOT NULL,claim_count INTEGER NOT NULL CHECK(claim_count>=0),"
            "outcome TEXT NOT NULL CHECK(outcome IN('pending','committed','aborted')),final_revision INTEGER,"
            "CHECK((outcome IN('pending','aborted') AND final_revision IS NULL) OR "
            "(outcome='committed' AND final_revision IS NOT NULL AND final_revision=expected_revision+1)))",
            "CREATE TABLE qc_sample_hold(slot_token TEXT PRIMARY KEY,change_token TEXT NOT NULL REFERENCES "
            "qc_sample_change(change_token) ON DELETE RESTRICT,control_id INTEGER NOT NULL REFERENCES quality_control_list(id) "
            "ON DELETE RESTRICT,batch_id INTEGER NOT NULL REFERENCES quality_control_batch(id) ON DELETE RESTRICT,"
            "control_name TEXT NOT NULL,batch_name TEXT NOT NULL,sample_id TEXT NOT NULL)",
            "CREATE TABLE qc_sample_hold_project(slot_token TEXT NOT NULL REFERENCES qc_sample_hold(slot_token) ON DELETE CASCADE,"
            "position INTEGER NOT NULL CHECK(position>=0),association_id INTEGER NOT NULL REFERENCES quality_item_proj(id) ON DELETE "
            "RESTRICT,project_id INTEGER NOT NULL,PRIMARY KEY(slot_token,position),UNIQUE(slot_token,association_id),UNIQUE(slot_token,project_id))",
            "CREATE INDEX qc_sample_change_owner ON qc_sample_change(owner_dataset_uuid)",
            "CREATE INDEX qc_sample_hold_change ON qc_sample_hold(change_token)",
            "CREATE INDEX qc_sample_hold_control ON qc_sample_hold(control_id)",
            "CREATE INDEX qc_sample_hold_batch ON qc_sample_hold(batch_id)",
            "CREATE INDEX qc_sample_hold_association ON qc_sample_hold_project(association_id)",
            "CREATE TRIGGER qc_sample_control_guard BEFORE UPDATE OF item_name ON quality_control_list WHEN NEW.item_name<>OLD.item_name "
            "AND EXISTS(SELECT 1 FROM qc_sample_hold WHERE control_id=OLD.id) BEGIN SELECT RAISE(ABORT,'质控品有样本引用或待定预约，不能改名'); END",
            "CREATE TRIGGER qc_sample_batch_guard BEFORE UPDATE OF batch_name,fid ON quality_control_batch WHEN "
            "(NEW.batch_name<>OLD.batch_name OR NEW.fid<>OLD.fid) AND EXISTS(SELECT 1 FROM qc_sample_hold WHERE batch_id=OLD.id) "
            "BEGIN SELECT RAISE(ABORT,'批次有样本引用或待定预约，不能变更身份'); END",
            "CREATE TRIGGER qc_sample_association_guard BEFORE UPDATE OF item_id,project_id,proj_name ON quality_item_proj WHEN "
            "(NEW.item_id<>OLD.item_id OR NEW.project_id<>OLD.project_id OR NEW.proj_name<>OLD.proj_name) AND EXISTS"
            "(SELECT 1 FROM qc_sample_hold_project WHERE association_id=OLD.id) BEGIN SELECT RAISE(ABORT,'项目关联有样本引用或待定预约'); END"
        };
        for (const QString &sql : statements) if (!execute(sql, {}, message)) return false;
        if (!execute("INSERT INTO qc_metadata VALUES('sample_reference_schema',?)", {sampleReferenceSchema}, message)) return false;
    }
    const QStringList checks = {
        "SELECT owner_dataset_uuid,catalog_path FROM qc_sample_owner LIMIT 0",
        "SELECT change_token,owner_dataset_uuid,quality_dataset_uuid,expected_revision,claims_digest,claim_count,outcome,final_revision FROM qc_sample_change LIMIT 0",
        "SELECT slot_token,change_token,control_id,batch_id,control_name,batch_name,sample_id FROM qc_sample_hold LIMIT 0",
        "SELECT slot_token,position,association_id,project_id FROM qc_sample_hold_project LIMIT 0"};
    for (const QString &sql : checks) if (!execute(sql, {}, message)) return false;
    if (!sampleRun(&marker, "PRAGMA foreign_key_check", {}, message)) return false;
    const bool broken = marker.next();
    if (!sampleFetched(marker, message)) return false;
    return !broken || sampleReject(message, QStringLiteral("质控库外键关联损坏，保护结构未就绪。"));
}

bool QcStore::beginSamplePair(QString *message)
{
    if (QThread::currentThread() != thread() || !ready_ || !referencesReady_ ||
        !database_.isOpen() || !catalogDatabase_.isOpen())
        return sampleReject(message, QStringLiteral("质控存储或已批准的项目库绑定尚未就绪。"));
    if (!begin(message)) return false;
    QSqlQuery query(catalogDatabase_);
    if (!sampleRun(&query, "BEGIN IMMEDIATE", {}, message)) { database_.rollback(); return false; }
    QString stored, bound;
    const QString qcPath = QFileInfo(database_.databaseName()).canonicalFilePath();
    bool ok = sampleMetadata(database_, "qc_metadata", "dataset_uuid", &stored, message) && stored == datasetId_ &&
        sampleMetadata(catalogDatabase_, "catalog_metadata", "quality_control_dataset_path:" + datasetId_, &bound, message) &&
        !qcPath.isEmpty() && bound == qcPath;
    if (!ok)
    {
        if (message->isEmpty()) *message = QStringLiteral("质控UUID或规范文件路径绑定已变化，保留预约。");
        rollbackSamplePair(); return false;
    }
    for (QSqlDatabase *db : {&database_, &catalogDatabase_})
    {
        QSqlQuery keys(*db);
        if (!sampleRun(&keys, "PRAGMA foreign_keys", {}, message) || !keys.next() || keys.value(0).toInt() != 1)
        { rollbackSamplePair(); return sampleReject(message, QStringLiteral("外键保护未开启，禁止质控样本操作。")); }
    }
    return true;
}
void QcStore::rollbackSamplePair()
{
    catalogDatabase_.rollback();
    database_.rollback();
}
bool QcStore::commitSamplePair(QString *message)
{
    // 顺序与现行QC关联保护一致；跨两个SQLite文件不宣称原子提交。
    if (!catalogDatabase_.commit())
    { *message = catalogDatabase_.lastError().text(); rollbackSamplePair(); return false; }
    if (!commit(message)) { database_.rollback(); return false; }
    return true;
}
bool QcStore::verifySampleOwner(QString owner, QString path, bool allowBind, QString *message)
{
    const QString actualPath = QFileInfo(catalogDatabase_.databaseName()).canonicalFilePath();
    if (!sampleUuid(owner) || path.isEmpty() || path != actualPath)
        return sampleReject(message, QStringLiteral("样本UUID或请求路径不属于当前已绑定catalog，未打开其他文件。"));
    QString stored, version;
    if (!sampleMetadata(catalogDatabase_, "catalog_metadata", "sample_dataset_uuid", &stored, message) ||
        !sampleMetadata(catalogDatabase_, "catalog_metadata", "sample_qc_schema", &version, message)) return false;
    if (stored != owner || version != "sn100-sample-qc-local-v1")
        return sampleReject(message, QStringLiteral("样本数据集身份或结构版本不匹配，禁止自动改号。"));
    qint64 revision = 0;
    if (!sampleRevision(catalogDatabase_, &revision, message)) return false;
    QSqlQuery query(database_);
    if (!sampleRun(&query, "SELECT catalog_path FROM qc_sample_owner WHERE owner_dataset_uuid=?", {owner}, message)) return false;
    const bool found = query.next();
    if (!sampleFetched(query, message)) return false;
    if (found)
        return query.value(0).toString() == actualPath ||
            sampleReject(message, QStringLiteral("样本UUID已绑定另一物理文件，保留副本和保护，需显式核对恢复。"));
    if (!allowBind) return sampleReject(message, QStringLiteral("样本引用所属数据集未登记，禁止释放。"));
    return execute("INSERT INTO qc_sample_owner VALUES(?,?)", {owner,actualPath}, message, 1);
}

bool QcStore::readSampleChoicesLocked(SampleQcPreset::Snapshot *output, QString *message)
{
    SampleQcPreset::Snapshot snapshot;
    snapshot.datasetId = datasetId_;
    QSqlQuery projects(catalogDatabase_);
    if (!sampleRun(&projects, "SELECT id,project_en_name,is_used FROM project_base ORDER BY id", {}, message)) return false;
    while (projects.next())
    {
        SampleQcPreset::Project project;
        project.id = projects.value(0).toLongLong(); project.name = projects.value(1).toString();
        project.enabled = projects.value(2).toBool(); snapshot.projects.append(project);
    }
    if (!sampleFetched(projects, message)) return false;
    QSqlQuery controls(database_);
    if (!sampleRun(&controls, "SELECT id,item_name FROM quality_control_list ORDER BY id", {}, message)) return false;
    while (controls.next())
    {
        SampleQcPreset::Control control;
        control.id = controls.value(0).toLongLong(); control.name = controls.value(1).toString();
        QSqlQuery batches(database_);
        if (!sampleRun(&batches, "SELECT id,batch_name FROM quality_control_batch WHERE fid=? ORDER BY id", {control.id}, message)) return false;
        while (batches.next())
        {
            SampleQcPreset::Batch batch;
            batch.id = batches.value(0).toLongLong(); batch.controlId = control.id;
            batch.name = batches.value(1).toString(); control.batches.append(batch);
        }
        if (!sampleFetched(batches, message)) return false;
        QSqlQuery associations(database_);
        if (!sampleRun(&associations, "SELECT id,project_id,proj_name FROM quality_item_proj WHERE item_id=? ORDER BY id", {control.id}, message)) return false;
        while (associations.next())
        {
            SampleQcPreset::Association association;
            association.id = associations.value(0).toLongLong(); association.controlId = control.id;
            association.projectId = associations.value(1).toLongLong(); association.projectName = associations.value(2).toString();
            control.projects.append(association);
        }
        if (!sampleFetched(associations, message)) return false;
        snapshot.controls.append(control);
    }
    if (!sampleFetched(controls, message)) return false;
    snapshot.ready = true;
    *output = snapshot;
    return true;
}
void QcStore::loadSampleChoices(qint64 requestId)
{
    QString message;
    if (!beginSamplePair(&message)) { emit sampleChoicesError(requestId, message); return; }
    SampleQcPreset::Snapshot snapshot;
    if (!readSampleChoicesLocked(&snapshot, &message) || !commitSamplePair(&message))
    { rollbackSamplePair(); emit sampleChoicesError(requestId, message); return; }
    emit sampleChoicesReady(requestId, snapshot);
}

bool QcStore::sampleIdentityUnheld(QString kind, qint64 id, QString *message)
{
    QString sql;
    if (kind == "control") sql = "SELECT 1 FROM qc_sample_hold WHERE control_id=? LIMIT 1";
    else if (kind == "batch") sql = "SELECT 1 FROM qc_sample_hold WHERE batch_id=? LIMIT 1";
    else if (kind == "association") sql = "SELECT 1 FROM qc_sample_hold_project WHERE association_id=? LIMIT 1";
    else return sampleReject(message, QStringLiteral("未知质控身份检查，禁止忽略引用。"));
    QSqlQuery query(database_);
    if (!sampleRun(&query, sql, {id}, message)) return false;
    const bool held = query.next();
    if (!sampleFetched(query, message)) return false;
    return !held || sampleReject(message, QStringLiteral("该质控身份被样本预设或待定预约引用，不能改名、删除或解除关联。"));
}

bool QcStore::readSampleIdentitiesLocked(QList<SampleQcIdentity> *output, QString *message)
{
    QList<SampleQcIdentity> identities;
    QSet<QString> tokens;
    QSqlQuery query(catalogDatabase_);
    if (!sampleRun(&query, "SELECT q.rack_id,q.hole_number,q.sample_type,q.dataset_uuid,q.control_id,q.batch_id,"
                   "q.control_name,q.batch_name,q.slot_token,q.change_token,h.sample_id FROM sample_local_quality_hole q "
                   "LEFT JOIN sample_local_hole h ON h.rack_id=q.rack_id AND h.number=q.hole_number ORDER BY q.rack_id,q.hole_number",
                   {}, message)) return false;
    while (query.next())
    {
        SampleQcIdentity identity;
        auto &selected = identity.selected;
        const qint64 rack = query.value(0).toLongLong();
        const int hole = query.value(1).toInt();
        selected.datasetId = query.value(3).toString(); selected.controlId = query.value(4).toLongLong();
        selected.batchId = query.value(5).toLongLong(); selected.controlName = query.value(6).toString();
        selected.batchName = query.value(7).toString(); identity.slotToken = query.value(8).toString();
        identity.changeToken = query.value(9).toString(); selected.sampleId = query.value(10).toString();
        if (rack <= 0 || hole <= 0 || query.value(2).toInt() != 2 || query.value(10).isNull() ||
            selected.datasetId != datasetId_ || selected.controlId <= 0 || selected.batchId <= 0 ||
            selected.controlName.isEmpty() || selected.batchName.isEmpty() ||
            selected.sampleId != selected.controlName + "-" + selected.batchName ||
            !sampleUuid(identity.slotToken) || !sampleUuid(identity.changeToken) || tokens.contains(identity.slotToken))
            return sampleReject(message, QStringLiteral("已保存质控孔身份、来源或派生样本号损坏，未按名称猜归属。"));
        tokens.insert(identity.slotToken);
        QSqlQuery projects(catalogDatabase_);
        if (!sampleRun(&projects, "SELECT position,association_id,project_id FROM sample_local_quality_project "
                       "WHERE rack_id=? AND hole_number=? ORDER BY position", {rack,hole}, message)) return false;
        QSet<qint64> associationIds, projectIds;
        while (projects.next())
        {
            const qint64 association = projects.value(1).toLongLong(), project = projects.value(2).toLongLong();
            if (projects.value(0).toInt() != selected.projects.size() || association <= 0 || project <= 0 ||
                associationIds.contains(association) || projectIds.contains(project))
                return sampleReject(message, QStringLiteral("质控孔项目顺序或稳定ID损坏。"));
            selected.projects.append({association,project}); associationIds.insert(association); projectIds.insert(project);
        }
        if (!sampleFetched(projects, message)) return false;
        QSqlQuery tasks(catalogDatabase_);
        if (!sampleRun(&tasks, "SELECT project_id FROM sample_local_task WHERE rack_id=? AND hole_number=?", {rack,hole}, message)) return false;
        QSet<qint64> taskIds;
        while (tasks.next()) taskIds.insert(tasks.value(0).toLongLong());
        if (!sampleFetched(tasks, message)) return false;
        if (taskIds != projectIds) return sampleReject(message, QStringLiteral("质控稳定关联与实际孔任务不一致，保留保护。"));
        identities.append(identity);
    }
    if (!sampleFetched(query, message)) return false;
    *output = identities;
    return true;
}

bool QcStore::sampleHoldMatches(QString owner, const SampleQcIdentity &identity, QString *message)
{
    const auto &selected = identity.selected;
    QSqlQuery hold(database_);
    if (!sampleRun(&hold, "SELECT c.owner_dataset_uuid,c.quality_dataset_uuid,h.change_token,h.control_id,h.batch_id,"
                   "h.control_name,h.batch_name,h.sample_id,l.item_name,b.batch_name,b.fid FROM qc_sample_hold h "
                   "JOIN qc_sample_change c ON c.change_token=h.change_token LEFT JOIN quality_control_list l ON l.id=h.control_id "
                   "LEFT JOIN quality_control_batch b ON b.id=h.batch_id WHERE h.slot_token=?", {identity.slotToken}, message)) return false;
    const bool found = hold.next();
    if (!sampleFetched(hold, message)) return false;
    if (!found || hold.value(0).toString() != owner || hold.value(1).toString() != datasetId_ ||
        hold.value(2).toString() != identity.changeToken || selected.datasetId != datasetId_ ||
        hold.value(3).toLongLong() != selected.controlId || hold.value(4).toLongLong() != selected.batchId ||
        hold.value(5).toString() != selected.controlName || hold.value(6).toString() != selected.batchName ||
        hold.value(7).toString() != selected.sampleId || hold.value(8).toString() != selected.controlName ||
        hold.value(9).toString() != selected.batchName || hold.value(10).toLongLong() != selected.controlId)
        return sampleReject(message, QStringLiteral("质控孔缺匹配持久保护或当前身份已改变，需核对恢复来源。"));
    QSqlQuery projects(database_);
    if (!sampleRun(&projects, "SELECT p.position,p.association_id,p.project_id,a.item_id,a.project_id,a.proj_name "
                   "FROM qc_sample_hold_project p LEFT JOIN quality_item_proj a ON a.id=p.association_id "
                   "WHERE p.slot_token=? ORDER BY p.position", {identity.slotToken}, message)) return false;
    int position = 0;
    while (projects.next())
    {
        if (position >= selected.projects.size() || projects.value(0).toInt() != position ||
            projects.value(1).toLongLong() != selected.projects[position].associationId ||
            projects.value(2).toLongLong() != selected.projects[position].projectId ||
            projects.value(3).toLongLong() != selected.controlId ||
            projects.value(4).toLongLong() != selected.projects[position].projectId)
            return sampleReject(message, QStringLiteral("已保存质控项目与保护/父关联不匹配。"));
        QSqlQuery catalog(catalogDatabase_);
        if (!sampleRun(&catalog, "SELECT project_en_name FROM project_base WHERE id=?",
                       {selected.projects[position].projectId}, message)) return false;
        const bool projectExists = catalog.next();
        if (!sampleFetched(catalog, message)) return false;
        if (!projectExists || catalog.value(0).toString() != projects.value(5).toString())
            return sampleReject(message, QStringLiteral("被引用项目缺失或名称错配，未清理质控保护。"));
        catalog.finish();
        if (!sampleRun(&catalog, "SELECT project_id FROM catalog_project_reference WHERE owner_module='quality_control' AND reference_key=?",
                       {referenceKey(selected.controlId,selected.projects[position].projectId)}, message)) return false;
        const bool projectProtected = catalog.next();
        if (!sampleFetched(catalog, message)) return false;
        if (!projectProtected || catalog.value(0).toLongLong() != selected.projects[position].projectId)
            return sampleReject(message, QStringLiteral("质控关联缺匹配项目字典保护，需显式重新绑定对账。"));
        ++position;
    }
    if (!sampleFetched(projects, message)) return false;
    return position == selected.projects.size() || sampleReject(message, QStringLiteral("质控项目保护数量不完整。"));
}

void QcStore::reserveSampleChange(SampleQcChange change)
{
    SampleQcReservationReply reply;
    reply.changeToken = change.changeToken; reply.ownerDatasetId = change.ownerDatasetId;
    reply.qualityDatasetId = change.qualityDatasetId;
    QString message;
    const auto failRequest = [&]() {
        rollbackSamplePair(); reply.message = message; emit sampleChangeReserved(reply);
    };
    if (!sampleUuid(change.changeToken) || !sampleUuid(change.ownerDatasetId) ||
        !sampleUuid(change.qualityDatasetId) || change.expectedRevision < 0 ||
        change.expectedRevision == std::numeric_limits<qint64>::max())
    { reply.message = QStringLiteral("质控引用变更身份或预期修订号无效。"); emit sampleChangeReserved(reply); return; }
    if (!beginSamplePair(&message)) { reply.message = message; emit sampleChangeReserved(reply); return; }
    if (change.qualityDatasetId != datasetId_ ||
        !verifySampleOwner(change.ownerDatasetId, change.catalogCanonicalPath, true, &message))
    { if (message.isEmpty()) message = QStringLiteral("目标质控数据集已改变。"); failRequest(); return; }
    qint64 revision = 0;
    SampleSourceOutcome outcome;
    if (!sampleRevision(catalogDatabase_, &revision, &message) ||
        !sampleOutcome(catalogDatabase_, change.changeToken, &outcome, &message))
    { failRequest(); return; }
    if (!outcome.exists || outcome.owner != change.ownerDatasetId || outcome.quality != datasetId_ ||
        outcome.expected != change.expectedRevision || outcome.outcome != "pending" || revision != change.expectedRevision)
    { message = QStringLiteral("样本待定结局、数据集或修订已变化，未持有新引用。请核对保存结局。"); failRequest(); return; }
    QList<SampleQcIdentity> committed;
    if (!readSampleIdentitiesLocked(&committed, &message)) { failRequest(); return; }
    for (const auto &identity : committed)
        if (!sampleHoldMatches(change.ownerDatasetId, identity, &message)) { failRequest(); return; }
    SampleQcPreset::Snapshot snapshot;
    if (!readSampleChoicesLocked(&snapshot, &message)) { failRequest(); return; }
    QSet<QString> slots;
    for (const SampleQcClaim &claim : change.claims)
    {
        const auto checked = SampleQcPreset::recheck(snapshot, claim.selected);
        if (!sampleUuid(claim.slotToken) || slots.contains(claim.slotToken) || !checked.ok())
        { message = checked.ok() ? QStringLiteral("孔位引用token无效或重复。") : checked.message; failRequest(); return; }
        slots.insert(claim.slotToken);
    }
    const QString digest = sampleClaimsDigest(change);
    QSqlQuery existing(database_);
    if (!sampleRun(&existing, "SELECT owner_dataset_uuid,quality_dataset_uuid,expected_revision,claims_digest,claim_count,outcome "
                   "FROM qc_sample_change WHERE change_token=?", {change.changeToken}, &message)) { failRequest(); return; }
    const bool recorded = existing.next();
    if (!sampleFetched(existing, &message)) { failRequest(); return; }
    if (recorded && (existing.value(0).toString() != change.ownerDatasetId || existing.value(1).toString() != datasetId_ ||
        existing.value(2).toLongLong() != change.expectedRevision || existing.value(3).toString() != digest ||
        existing.value(4).toInt() != change.claims.size() || existing.value(5).toString() != "pending"))
    { message = QStringLiteral("变更token已用于另一组身份或已有最终结局，禁止改绑。"); failRequest(); return; }
    existing.finish();
    if (!recorded && !execute("INSERT INTO qc_sample_change VALUES(?,?,?,?,?,?,'pending',NULL)",
        {change.changeToken,change.ownerDatasetId,datasetId_,change.expectedRevision,digest,change.claims.size()}, &message, 1))
    { failRequest(); return; }
    for (const SampleQcClaim &claim : change.claims)
    {
        SampleQcIdentity identity;
        identity.slotToken = claim.slotToken; identity.changeToken = change.changeToken; identity.selected = claim.selected;
        if (recorded)
        {
            if (!sampleHoldMatches(change.ownerDatasetId, identity, &message)) { failRequest(); return; }
            continue;
        }
        const auto &selected = claim.selected;
        if (!execute("INSERT INTO qc_sample_hold VALUES(?,?,?,?,?,?,?)", {claim.slotToken,change.changeToken,
            selected.controlId,selected.batchId,selected.controlName,selected.batchName,selected.sampleId}, &message))
        { failRequest(); return; }
        int position = 0;
        for (const auto &project : selected.projects)
            if (!execute("INSERT INTO qc_sample_hold_project VALUES(?,?,?,?)",
                         {claim.slotToken,position++,project.associationId,project.projectId}, &message, 1))
            { failRequest(); return; }
        if (!sampleHoldMatches(change.ownerDatasetId, identity, &message)) { failRequest(); return; }
    }
    QSqlQuery count(database_);
    if (!sampleRun(&count, "SELECT COUNT(*) FROM qc_sample_hold WHERE change_token=?", {change.changeToken}, &message) ||
        !count.next() || count.value(0).toInt() != change.claims.size())
    { if (message.isEmpty()) message = QStringLiteral("预约孔数不完整，未返回成功。"); failRequest(); return; }
    count.finish();
    if (!commitSamplePair(&message)) { reply.message = message; emit sampleChangeReserved(reply); return; }
    reply.reserved = true; reply.claims = change.claims;
    emit sampleChangeReserved(reply);
}

bool QcStore::reconcileSampleReferencesLocked(QString owner, QString *message)
{
    for (QSqlDatabase *db : {&database_, &catalogDatabase_})
    {
        QSqlQuery integrity(*db);
        if (!sampleRun(&integrity, "PRAGMA foreign_key_check", {}, message)) return false;
        const bool broken = integrity.next();
        if (!sampleFetched(integrity, message)) return false;
        if (broken) return sampleReject(message, QStringLiteral("引用关联损坏，未清理任何保护。"));
    }
    qint64 currentRevision = 0;
    if (!sampleRevision(catalogDatabase_, &currentRevision, message)) return false;
    QList<SampleQcIdentity> source;
    if (!readSampleIdentitiesLocked(&source, message)) return false;
    QMap<QString,SampleQcIdentity> live;
    for (const auto &identity : source)
    {
        if (!sampleHoldMatches(owner, identity, message)) return false;
        live.insert(identity.slotToken, identity);
    }
    struct SavedChange
    {
        QString token, outcome;
        qint64 expected = 0;
        QVariant finalRevision;
        int claims = 0;
    };
    QList<SavedChange> changes;
    QSet<QString> targetTokens;
    QMap<QString,QString> finalStates;
    QSqlQuery rows(database_);
    if (!sampleRun(&rows, "SELECT change_token,quality_dataset_uuid,expected_revision,outcome,final_revision,claim_count "
                   "FROM qc_sample_change WHERE owner_dataset_uuid=? ORDER BY change_token", {owner}, message)) return false;
    while (rows.next())
    {
        SavedChange change;
        change.token = rows.value(0).toString(); change.expected = rows.value(2).toLongLong();
        change.outcome = rows.value(3).toString(); change.finalRevision = rows.value(4); change.claims = rows.value(5).toInt();
        if (!sampleUuid(change.token) || rows.value(1).toString() != datasetId_ || change.expected < 0 || change.claims < 0 ||
            (change.outcome != "pending" && change.outcome != "committed" && change.outcome != "aborted"))
            return sampleReject(message, QStringLiteral("QC预约头身份或结局损坏，保留保护。"));
        if ((change.outcome != "committed" && !change.finalRevision.isNull()) ||
            (change.outcome == "committed" && (change.finalRevision.isNull() || change.expected == std::numeric_limits<qint64>::max() ||
             change.finalRevision.toLongLong() != change.expected + 1)))
            return sampleReject(message, QStringLiteral("QC已核实结局的修订号损坏，不能视为未保存。"));
        if (!change.finalRevision.isNull() &&
            (change.finalRevision.toLongLong() < 0 || change.finalRevision.toLongLong() > currentRevision))
            return sampleReject(message, QStringLiteral("样本修订落后于已核实结局，疑似错配恢复，保留保护。"));
        changes.append(change); targetTokens.insert(change.token);
    }
    if (!sampleFetched(rows, message)) return false;
    rows.finish();
    int unresolved = 0;
    for (const SavedChange &change : changes)
    {
        SampleSourceOutcome actual;
        if (!sampleOutcome(catalogDatabase_, change.token, &actual, message)) return false;
        if (actual.exists && (actual.owner != owner || actual.quality != datasetId_ || actual.expected != change.expected))
            return sampleReject(message, QStringLiteral("源和目标的预约家庭身份/预期修订不一致，未清理。"));
        if (actual.exists && !actual.finalRevision.isNull() && actual.finalRevision.toLongLong() > currentRevision)
            return sampleReject(message, QStringLiteral("源结局修订超过当前样本修订，需核对恢复。"));
        QString resolved = change.outcome;
        if (change.outcome == "pending")
        {
            QSqlQuery count(database_);
            if (!sampleRun(&count, "SELECT COUNT(*) FROM qc_sample_hold WHERE change_token=?", {change.token}, message) || !count.next()) return false;
            if (count.value(0).toInt() != change.claims)
                return sampleReject(message, QStringLiteral("待定预约孔数不完整，禁止按缺行自动修复。"));
            if (!actual.exists || actual.outcome == "pending") { ++unresolved; continue; }
            resolved = actual.outcome;
            if (!execute("UPDATE qc_sample_change SET outcome=?,final_revision=? WHERE change_token=? AND outcome='pending'",
                         {resolved,actual.finalRevision,change.token}, message, 1)) return false;
        }
        else if (actual.exists && (actual.outcome != change.outcome || actual.finalRevision != change.finalRevision))
            return sampleReject(message, QStringLiteral("已核实QC结局与源日志冲突，禁止覆盖结局。"));
        // 源只会在收到真实reconciled回执后删除自己的结局行；QC侧最终证明保留。
        finalStates.insert(change.token, resolved);
    }
    QSqlQuery sourceChanges(catalogDatabase_);
    if (!sampleRun(&sourceChanges, "SELECT change_token FROM sample_local_quality_change WHERE owner_uuid=?",
                   {owner}, message)) return false;
    QStringList sourceTokens;
    while (sourceChanges.next()) sourceTokens.append(sourceChanges.value(0).toString());
    if (!sampleFetched(sourceChanges, message)) return false;
    sourceChanges.finish();
    for (const QString &token : sourceTokens)
    {
        if (!sampleUuid(token)) return sampleReject(message, QStringLiteral("源变更token损坏。"));
        if (targetTokens.contains(token)) continue;
        SampleSourceOutcome actual;
        if (!sampleOutcome(catalogDatabase_, token, &actual, message)) return false;
        if (!actual.exists || actual.owner != owner || actual.quality != datasetId_)
            return sampleReject(message, QStringLiteral("源变更属于另一QC数据集，未按同名内容恢复。"));
        if (actual.outcome == "pending") { ++unresolved; continue; }
        if (actual.outcome == "committed")
            return sampleReject(message, QStringLiteral("已提交样本变更缺QC预约头，保留资料并核对恢复来源。"));
        if (!actual.finalRevision.isNull() && actual.finalRevision.toLongLong() > currentRevision)
            return sampleReject(message, QStringLiteral("取消结局的最终修订超出当前样本库，未清理。"));
        // 在QC预约失败之前，Sample可能已将自己的pending真实改为aborted。
        // 只登记已证实无QC预约的取消结局，便于Finish确认后让Sample清掉源凭据。
        if (!execute("INSERT INTO qc_sample_change VALUES(?,?,?,?,?,?,'aborted',?)",
                     {token,owner,datasetId_,actual.expected,QStringLiteral("aborted-before-target-reservation"),0,actual.finalRevision}, message, 1)) return false;
        finalStates.insert(token, "aborted");
    }
    for (const auto &identity : source)
        if (finalStates.value(identity.changeToken) != "committed")
            return sampleReject(message, QStringLiteral("实际已保存质控孔没有已提交结局证明，保留保护。"));
    if (unresolved > 0)
        return sampleReject(message, QStringLiteral("有%1条预约仍待定或缺源结局；未清理保护，也未猜测保存失败。").arg(unresolved));

    QSqlQuery held(database_);
    if (!sampleRun(&held, "SELECT h.slot_token,h.change_token FROM qc_sample_hold h JOIN qc_sample_change c "
                   "ON c.change_token=h.change_token WHERE c.owner_dataset_uuid=?", {owner}, message)) return false;
    QStringList obsolete;
    while (held.next())
    {
        const QString slot = held.value(0).toString(), token = held.value(1).toString();
        if (live.contains(slot)) continue;
        const QString state = finalStates.value(token);
        if (state != "committed" && state != "aborted")
            return sampleReject(message, QStringLiteral("废弃候选引用没有最终结局，禁止释放。"));
        obsolete.append(slot);
    }
    if (!sampleFetched(held, message)) return false;
    held.finish();
    for (const QString &slot : obsolete)
        if (!execute("DELETE FROM qc_sample_hold WHERE slot_token=?", {slot}, message, 1)) return false;
    return true;
}

void QcStore::finishSampleChange(QString token, bool callerCommitted, qint64 observedRevision)
{
    SampleQcFinishReply reply;
    reply.changeToken = token; reply.qualityDatasetId = datasetId_;
    QString message;
    if (!sampleUuid(token))
    { reply.message = QStringLiteral("引用变更token无效。"); emit sampleChangeFinished(reply); return; }
    if (!beginSamplePair(&message)) { reply.message = message; emit sampleChangeFinished(reply); return; }
    QSqlQuery target(database_);
    if (!sampleRun(&target, "SELECT owner_dataset_uuid,quality_dataset_uuid FROM qc_sample_change "
                   "WHERE change_token=?", {token}, &message))
    { rollbackSamplePair(); reply.message = message; emit sampleChangeFinished(reply); return; }
    const bool found = target.next();
    if (!sampleFetched(target, &message))
    { rollbackSamplePair(); reply.message = message; emit sampleChangeFinished(reply); return; }
    if (found)
    {
        reply.ownerDatasetId = target.value(0).toString();
        reply.qualityDatasetId = target.value(1).toString();
    }
    target.finish();
    SampleSourceOutcome actual;
    if (!sampleOutcome(catalogDatabase_, token, &actual, &message))
    { rollbackSamplePair(); reply.message = message; emit sampleChangeFinished(reply); return; }
    if (!found && actual.exists)
    { reply.ownerDatasetId = actual.owner; reply.qualityDatasetId = actual.quality; }
    if ((!found && !actual.exists) || reply.qualityDatasetId != datasetId_ ||
        !verifySampleOwner(reply.ownerDatasetId, QFileInfo(catalogDatabase_.databaseName()).canonicalFilePath(), true, &message))
    {
        rollbackSamplePair(); reply.message = message.isEmpty() ? QStringLiteral("源和目标都无匹配变更身份，未假定可释放。") : message;
        emit sampleChangeFinished(reply); return;
    }
    if (actual.exists && (actual.owner != reply.ownerDatasetId || actual.quality != datasetId_))
    { rollbackSamplePair(); reply.message = QStringLiteral("源结局家庭身份与QC预约不一致。"); emit sampleChangeFinished(reply); return; }
    // 旧的真实ACK后Source可条件清掉终态标记；此后重复Finish也不能凭缺行再造ACK。
    // Owner启动对账仍可利用QC已提交的历史核实证明检查现存slot，而不是猜源失败。
    if (!actual.exists)
    { rollbackSamplePair(); reply.message = QStringLiteral("源结局标记缺失，保留保护；未把缺行或旧修订当取消。"); emit sampleChangeFinished(reply); return; }
    const QString proved = actual.outcome;
    if (proved == "committed") reply.outcome = SampleQcCatalogOutcome::Committed;
    else if (proved == "aborted") reply.outcome = SampleQcCatalogOutcome::NotCommitted;
    if (proved != "committed" && proved != "aborted")
    { rollbackSamplePair(); reply.message = QStringLiteral("真实源结局仍待定，保留预约；调用方提示不能代替结局证明。"); emit sampleChangeFinished(reply); return; }
    if (!reconcileSampleReferencesLocked(reply.ownerDatasetId, &message) || !commitSamplePair(&message))
    { rollbackSamplePair(); reply.message = message; emit sampleChangeFinished(reply); return; }
    reply.reconciled = true;
    const QVariant finalRevision = actual.finalRevision;
    if (callerCommitted != (proved == "committed") ||
        (!finalRevision.isNull() && observedRevision != finalRevision.toLongLong()))
        reply.message = QStringLiteral("已按真实数据库核实结局；调用方保存/修订提示与数据库不同。");
    emit sampleChangeFinished(reply);
}

void QcStore::reconcileSampleOwner(QString owner, QString path)
{
    QString message;
    if (!beginSamplePair(&message)) { emit sampleOwnerReconciled(owner, datasetId_, false, message); return; }
    if (!verifySampleOwner(owner, path, true, &message) || !reconcileSampleReferencesLocked(owner, &message) ||
        !commitSamplePair(&message))
    { rollbackSamplePair(); emit sampleOwnerReconciled(owner, datasetId_, false, message); return; }
    emit sampleOwnerReconciled(owner, datasetId_, true, QString());
}
