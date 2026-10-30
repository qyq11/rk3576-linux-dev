#include "sample_store.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QStringList>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <limits>

namespace
{
bool sql(QSqlDatabase db, const QString &text, const QVariantList &values, QString *message,
         int expected = -1)
{
    QSqlQuery query(db);
    if (!query.prepare(text)) { *message = query.lastError().text(); return false; }
    for (const QVariant &value : values) query.addBindValue(value);
    if (!query.exec()) { *message = query.lastError().text(); return false; }
    if (expected >= 0 && query.numRowsAffected() != expected)
    { *message = QStringLiteral("预设已变化，未覆盖原记录。"); return false; }
    return true;
}
bool metadata(QSqlDatabase db, const QString &key, QString *value, QString *message)
{
    QSqlQuery query(db);
    query.prepare("SELECT value FROM catalog_metadata WHERE key=?"); query.addBindValue(key);
    if (!query.exec() || !query.next())
    { *message = QStringLiteral("缺少样本身份/版本：") + key + query.lastError().text(); return false; }
    *value = query.value(0).toString(); return true;
}
bool revision(QSqlDatabase db, qint64 *value, QString *message)
{
    QString text;
    if (!metadata(db, "sample_qc_revision", &text, message)) return false;
    bool valid = false; *value = text.toLongLong(&valid);
    if (!valid || *value < 0 || QString::number(*value) != text ||
        *value == std::numeric_limits<qint64>::max())
    { *message = QStringLiteral("样本修订号损坏或耗尽，禁止覆盖。"); return false; }
    return true;
}
bool containsQuality(const SampleRack &rack)
{
    for (const SampleHole &hole : rack.holes) if (hole.sampleType == 2) return true;
    return false;
}
bool locked(const SampleRack &rack)
{
    for (const SampleHole &hole : rack.holes)
        if (!hole.revise || sampleHoleState(sampleHoleTaskStates(hole)) == SampleHoleState::Testing) return true;
    return false;
}
QString identityProblem(const SampleHole &hole, bool persisted)
{
    const auto &identity = hole.quality;
    const auto &selected = identity.selected;
    if (hole.sampleType == 0)
    {
        if (!selected.datasetId.isEmpty() || selected.controlId || selected.batchId ||
            !selected.controlName.isEmpty() || !selected.batchName.isEmpty() || !selected.sampleId.isEmpty() ||
            !selected.projects.isEmpty() || !identity.slotToken.isEmpty() || !identity.changeToken.isEmpty())
            return QStringLiteral("普通样本不能残留质控身份。");
        return QString();
    }
    if (hole.sampleType != 2 || QUuid(selected.datasetId).isNull() || selected.controlId <= 0 ||
        selected.batchId <= 0 || selected.controlName.isEmpty() || selected.batchName.isEmpty() ||
        selected.sampleId != selected.controlName + "-" + selected.batchName || hole.sampleId != selected.sampleId ||
        (persisted && (QUuid(identity.slotToken).isNull() || QUuid(identity.changeToken).isNull())))
        return QStringLiteral("质控预设类型或稳定身份损坏，保留原数据。");
    QSet<qint64> projectIds, associationIds;
    for (const auto &project : selected.projects)
    {
        if (project.projectId <= 0 || project.associationId <= 0 || projectIds.contains(project.projectId) ||
            associationIds.contains(project.associationId)) return QStringLiteral("质控项目身份重复或无效。");
        projectIds.insert(project.projectId); associationIds.insert(project.associationId);
    }
    QSet<qint64> tasks;
    for (const auto &task : hole.tasks) tasks.insert(task.projectId);
    if (tasks != projectIds || tasks.size() != hole.tasks.size())
        return QStringLiteral("质控任务须与自动带出的关联项目一致，请回到明细重新选择。");
    return QString();
}
}

void SampleStore::setupQualityProtocol()
{
    qRegisterMetaType<SampleQcPreset::Snapshot>();
    qRegisterMetaType<SampleQcIdentity>(); qRegisterMetaType<SampleQcChange>();
    qRegisterMetaType<SampleQcReservationReply>(); qRegisterMetaType<SampleQcFinishReply>();
    qualityReplyTimer_ = new QTimer(this);
    qualityReplyTimer_->setSingleShot(true);
    // 本地引用回执的故障等待上限；不是业务响应或临床放行指标。
    qualityReplyTimer_->setInterval(10000);
    connect(qualityReplyTimer_, &QTimer::timeout, this, &SampleStore::qualityReplyTimedOut);
}

bool SampleStore::initializeQualitySchema(QString *message)
{
    QSqlQuery query(database_);
    if (!query.exec("SELECT value FROM catalog_metadata WHERE key='sample_qc_schema'"))
    { *message = query.lastError().text(); return false; }
    const bool marked = query.next();
    if (marked && query.value(0).toString() != "sn100-sample-qc-local-v1")
    { *message = QStringLiteral("样本质控结构版本不支持。"); return false; }
    query.finish();
    if (!query.exec("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name IN"
                    "('sample_local_quality_hole','sample_local_quality_project','sample_local_quality_change')") || !query.next())
    { *message = query.lastError().text(); return false; }
    if (query.value(0).toInt() != (marked ? 3 : 0))
    { *message = QStringLiteral("样本质控表和版本不一致，禁止自动重建。"); return false; }
    if (!marked)
    {
        QSqlQuery existingOwner(database_);
        if (!existingOwner.exec("SELECT value FROM catalog_metadata WHERE key='sample_dataset_uuid'"))
        {
            *message = existingOwner.lastError().text();
            return false;
        }
        const bool ownerExists = existingOwner.next();
        if (ownerExists && QUuid(existingOwner.value(0).toString()).isNull())
        {
            *message = QStringLiteral("已有样本UUID无效，禁止自动换号。");
            return false;
        }
        existingOwner.finish();
        QSqlQuery previousRevision(database_);
        if (!previousRevision.exec("SELECT 1 FROM catalog_metadata WHERE key='sample_qc_revision'") || previousRevision.next())
        { *message = QStringLiteral("发现无版本的质控修订号，保留原数据。"); return false; }
        const QStringList statements = {
            "CREATE TABLE sample_local_quality_hole(rack_id INTEGER NOT NULL,hole_number INTEGER NOT NULL,"
            "sample_type INTEGER NOT NULL CHECK(sample_type=2),dataset_uuid TEXT NOT NULL,control_id INTEGER NOT NULL,"
            "batch_id INTEGER NOT NULL,control_name TEXT NOT NULL,batch_name TEXT NOT NULL,slot_token TEXT NOT NULL UNIQUE,"
            "change_token TEXT NOT NULL,PRIMARY KEY(rack_id,hole_number),FOREIGN KEY(rack_id,hole_number) "
            "REFERENCES sample_local_hole(rack_id,number) ON DELETE CASCADE)",
            "CREATE TABLE sample_local_quality_project(rack_id INTEGER NOT NULL,hole_number INTEGER NOT NULL,position INTEGER NOT NULL,"
            "association_id INTEGER NOT NULL,project_id INTEGER NOT NULL REFERENCES project_base(id) ON DELETE RESTRICT,"
            "PRIMARY KEY(rack_id,hole_number,position),UNIQUE(rack_id,hole_number,project_id),"
            "FOREIGN KEY(rack_id,hole_number) REFERENCES sample_local_quality_hole(rack_id,hole_number) ON DELETE CASCADE)",
            // 删除/改回普通后没有QC行，凭据仍须留下；不能靠“查不到新行”推定事务结局。
            "CREATE TABLE sample_local_quality_change(change_token TEXT PRIMARY KEY,owner_uuid TEXT NOT NULL,"
            "quality_uuid TEXT NOT NULL,before_revision INTEGER NOT NULL CHECK(before_revision>=0),"
            "outcome TEXT NOT NULL CHECK(outcome IN('pending','committed','aborted')),after_revision INTEGER,"
            "operation TEXT NOT NULL CHECK(operation IN('saveRack','removeRack','clearOrdinaryRacks')),"
            "target_rack_id INTEGER NOT NULL CHECK(target_rack_id>=0),"
            "CHECK((outcome='pending' AND after_revision IS NULL) OR "
            "(outcome='committed' AND after_revision IS NOT NULL AND after_revision=before_revision+1) OR "
            "(outcome='aborted' AND after_revision IS NULL)))"};
        for (const QString &text : statements) if (!sql(database_, text, {}, message)) return false;
        if (!sql(database_, "INSERT INTO catalog_metadata VALUES('sample_qc_schema','sn100-sample-qc-local-v1')",{},message) ||
            (!ownerExists && !sql(database_, "INSERT INTO catalog_metadata VALUES('sample_dataset_uuid',?)",
                 {QUuid::createUuid().toString(QUuid::WithoutBraces)},message)) ||
            !sql(database_, "INSERT INTO catalog_metadata VALUES('sample_qc_revision','0')",{},message)) return false;
    }
    if (!metadata(database_, "sample_dataset_uuid", &qualityOwnerId_, message) || QUuid(qualityOwnerId_).isNull())
    { *message = QStringLiteral("样本数据集UUID无效，不能自动换号修复。"); return false; }
    query.finish();
    if (!query.exec("PRAGMA foreign_key_check") || query.next())
    {
        *message = QStringLiteral("本地目录存在损坏的关联，质控扩展未就绪。");
        return false;
    }
    qint64 current = 0;
    return revision(database_, &current, message) &&
           sql(database_, "SELECT sample_type,dataset_uuid,control_id,batch_id,control_name,batch_name,slot_token,change_token "
                          "FROM sample_local_quality_hole LIMIT 0",{},message) &&
           sql(database_, "SELECT position,association_id,project_id FROM sample_local_quality_project LIMIT 0",{},message) &&
           sql(database_, "SELECT change_token,owner_uuid,quality_uuid,before_revision,outcome,after_revision,operation,target_rack_id "
                          "FROM sample_local_quality_change LIMIT 0",{},message);
}

bool SampleStore::readQualityHole(qint64 rackId, SampleHole *hole, QString *message)
{
    QSqlQuery query(database_);
    query.prepare("SELECT sample_type,dataset_uuid,control_id,batch_id,control_name,batch_name,slot_token,change_token "
                  "FROM sample_local_quality_hole WHERE rack_id=? AND hole_number=?");
    query.addBindValue(rackId); query.addBindValue(hole->number);
    if (!query.exec()) { *message = query.lastError().text(); return false; }
    if (!query.next()) return true;
    bool validType = false;
    hole->sampleType = query.value(0).toInt(&validType);
    if (!validType) { *message=QStringLiteral("质控样本类型损坏。"); return false; }
    auto &identity = hole->quality;
    auto &selected = identity.selected;
    selected.datasetId = query.value(1).toString(); selected.controlId = query.value(2).toLongLong();
    selected.batchId = query.value(3).toLongLong(); selected.controlName = query.value(4).toString();
    selected.batchName = query.value(5).toString(); selected.sampleId = hole->sampleId;
    identity.slotToken = query.value(6).toString(); identity.changeToken = query.value(7).toString();
    query.finish();
    query.prepare("SELECT position,association_id,project_id FROM sample_local_quality_project "
                  "WHERE rack_id=? AND hole_number=? ORDER BY position");
    query.addBindValue(rackId); query.addBindValue(hole->number);
    if (!query.exec()) { *message = query.lastError().text(); return false; }
    int position = 0;
    while (query.next())
    {
        if (query.value(0).toInt() != position++) { *message = QStringLiteral("质控关联顺序损坏。"); return false; }
        selected.projects.append({query.value(1).toLongLong(), query.value(2).toLongLong()});
    }
    *message = identityProblem(*hole, true);
    return message->isEmpty();
}
QString SampleStore::qualityHoleProblem(const SampleHole &hole) const
{
    return identityProblem(hole, false);
}

void SampleStore::qualityOwnerReconciled(QString owner, QString qualityDatasetId, bool ready, QString message)
{
    if (QThread::currentThread() != thread() || owner != qualityOwnerId_ || !ready_) return;
    qualityBound_ = ready && !QUuid(qualityDatasetId).isNull();
    if (!QUuid(qualityDatasetId).isNull()) qualityTargetId_ = qualityDatasetId;
    emit qualityProtectionReady(qualityBound_);
    if (!qualityBound_) emit warning(0,"qualityReferences",message);
}

void SampleStore::invalidateQualityBinding()
{
    if (QThread::currentThread() != thread()) return;
    qualityBound_ = false;
    emit qualityProtectionReady(false);
    const QString reason = QStringLiteral("质控存储已关闭，引用保护待重新核对；原草稿保留。");
    if (pendingQuality_.stage == "reserve")
    {
        finishAbortedQualityChange(reason); // 仍先持久ABORTED；不靠关闭事件推定预约可释放。
    }
    if (!pendingQuality_.change.changeToken.isEmpty())
    {
        // 目标已关闭，不继续等待其ACK；终态凭据和保护留下供显式恢复。
        completeQualityChange(false, reason);
    }
    emit warning(0, "qualityReferences", reason);
}

bool SampleStore::rollbackQualityTransaction(QString *message, bool resolveCurrentWrite)
{
    if (database_.rollback()) return true;
    // SQLite可因RAISE(ROLLBACK)/I/O故障自行结束事务；false本身不能证明取消。
    // 只有当前QC业务写入可用同token的持久结局作正向核实，其余阶段仍保守关闭。
    if (resolveCurrentWrite && reconcileFailedQualityWrite(message)) return true;
    // 无法证实回滚时关闭自己的连接，释放catalog锁；不发“已放弃”假回执。
    *message += QStringLiteral("；回滚未确认，连接已关闭，持久结局及保护保留待恢复核对。");
    database_.close();
    ready_ = false;
    qualityBound_ = false;
    emit qualityProtectionReady(false);
    return false;
}

bool SampleStore::reconcileFailedQualityWrite(QString *message)
{
    const SampleQcChange &change = pendingQuality_.change;
    QString detail;
    if (!pendingQuality_.needsQuality || !ownQualityChanges_.contains(change.changeToken) ||
        change.ownerDatasetId != qualityOwnerId_ || change.qualityDatasetId != qualityTargetId_ ||
        change.catalogCanonicalPath.isEmpty() ||
        change.catalogCanonicalPath != QFileInfo(database_.databaseName()).canonicalFilePath())
    {
        *message += QStringLiteral("；不是本实例同一资料库的当前写入，未推断结局。");
        return false;
    }
    // 成功取得新写事务才证明旧事务已不活跃；随后仍须读同一持久凭据，不能仅凭BEGIN成功放弃。
    if (!sql(database_, "BEGIN IMMEDIATE", {}, &detail))
    {
        *message += QStringLiteral("；无法取得结局核对事务：") + detail;
        return false;
    }
    QString owner, schema, outcome;
    qint64 current = -1, finalRevision = -1, target = -1;
    bool ok = metadata(database_, "sample_dataset_uuid", &owner, &detail) &&
              metadata(database_, "sample_qc_schema", &schema, &detail) &&
              revision(database_, &current, &detail) &&
              readQualityOutcome(change.changeToken, &outcome, &finalRevision, &detail);
    if (ok && (owner != qualityOwnerId_ || schema != "sn100-sample-qc-local-v1"))
    {
        detail = QStringLiteral("样本资料库身份/结构已变化，未解释旧写入。");
        ok = false;
    }
    {
        QSqlQuery fence(database_);
        if (ok)
        {
            fence.prepare("SELECT before_revision,operation,target_rack_id FROM sample_local_quality_change "
                          "WHERE change_token=? AND owner_uuid=? AND quality_uuid=?");
            fence.addBindValue(change.changeToken); fence.addBindValue(qualityOwnerId_);
            fence.addBindValue(qualityTargetId_);
            ok = fence.exec() && fence.next();
            if (!ok) detail = QStringLiteral("当前写入的持久凭据不可核实：") + fence.lastError().text();
        }
        if (ok)
        {
            target = fence.value(2).toLongLong();
            ok = fence.value(0).toLongLong() == change.expectedRevision &&
                 fence.value(1).toString() == pendingQuality_.operation &&
                 ((outcome == "committed" && pendingQuality_.operation == "saveRack" &&
                   target > 0 && (!pendingQuality_.targetId || target == pendingQuality_.targetId)) ||
                  target == pendingQuality_.targetId);
            if (!ok) detail = QStringLiteral("写入凭据的修订/操作/目标架不一致。");
        }
    }
    if (ok)
    {
        ok = outcome == "committed" ? current == finalRevision : current == change.expectedRevision;
        if (!ok) detail = QStringLiteral("当前修订与该写入结局不一致，保留保护待恢复核对。");
    }
    if (ok && outcome != "committed")
    {
        QSqlQuery live(database_);
        live.prepare("SELECT COUNT(*) FROM sample_local_quality_hole WHERE change_token=?");
        live.addBindValue(change.changeToken);
        ok = live.exec() && live.next() && live.value(0).toLongLong() == 0;
        if (!ok) detail = QStringLiteral("未提交凭据与已保存质控孔冲突，禁止当作取消。");
    }
    if (ok && outcome == "pending")
        ok = sql(database_, "UPDATE sample_local_quality_change SET outcome='aborted',after_revision=NULL "
                 "WHERE change_token=? AND owner_uuid=? AND quality_uuid=? AND before_revision=? "
                 "AND outcome='pending' AND operation=? AND target_rack_id=?",
                 {change.changeToken,qualityOwnerId_,qualityTargetId_,change.expectedRevision,
                  pendingQuality_.operation,pendingQuality_.targetId}, &detail, 1);
    if (ok && database_.commit())
    {
        if (outcome == "committed" && pendingQuality_.operation == "saveRack")
            pendingQuality_.rack.id = target; // 只使用持久COMMITTED凭据的真实架ID。
        return true;
    }
    if (ok) detail = QStringLiteral("结局核对事务未确认提交：") + database_.lastError().text();
    database_.rollback();
    *message += QStringLiteral("；持久结局核对未确认：") + detail;
    return false;
}

bool SampleStore::persistPendingQualityChange(QString *message)
{
    const SampleQcChange &change = pendingQuality_.change;
    if (!sql(database_, "BEGIN IMMEDIATE", {}, message)) return false;
    qint64 current = -1;
    QString owner;
    bool ok = revision(database_, &current, message) &&
        metadata(database_, "sample_dataset_uuid", &owner, message);
    if (ok && (current != change.expectedRevision || owner != change.ownerDatasetId))
    {
        ok = false;
        *message = QStringLiteral("样本数据集或修订号已变化，未请求质控预约。");
    }
    if (ok)
    {
        ok = sql(database_, "INSERT INTO sample_local_quality_change "
            "(change_token,owner_uuid,quality_uuid,before_revision,outcome,after_revision,operation,target_rack_id) "
            "VALUES(?,?,?,?,'pending',NULL,?,?)",
            {change.changeToken, change.ownerDatasetId, change.qualityDatasetId, change.expectedRevision,
             pendingQuality_.operation, pendingQuality_.targetId}, message, 1);
    }
    if (ok && database_.commit()) return true;
    if (ok) *message = QStringLiteral("待定凭据提交未确认：") + database_.lastError().text();
    rollbackQualityTransaction(message);
    return false;
}

bool SampleStore::readQualityOutcome(QString token, QString *outcome, qint64 *finalRevision, QString *message)
{
    QSqlQuery query(database_);
    query.prepare("SELECT owner_uuid,quality_uuid,before_revision,outcome,after_revision,operation,target_rack_id "
                  "FROM sample_local_quality_change WHERE change_token=?");
    query.addBindValue(token);
    if (!query.exec() || !query.next())
    {
        *message = QStringLiteral("未找到可核对的源事务凭据：") + query.lastError().text();
        return false;
    }
    bool validRevision = false;
    const qint64 expected = query.value(2).toLongLong(&validRevision);
    if (query.value(0).toString() != qualityOwnerId_ || query.value(1).toString() != qualityTargetId_ ||
        !validRevision || expected < 0)
    {
        *message = QStringLiteral("源事务凭据的数据集身份或修订号不匹配。");
        return false;
    }
    *outcome = query.value(3).toString();
    const QString operation = query.value(5).toString();
    bool validTarget = false;
    const qint64 target = query.value(6).toLongLong(&validTarget);
    if (!validTarget || target < 0 || (operation != "saveRack" && operation != "removeRack" && operation != "clearOrdinaryRacks"))
    { *message = QStringLiteral("源凭据操作/目标无效。"); return false; }
    if (*outcome == "pending" || *outcome == "aborted")
    {
        if (!query.value(4).isNull())
        {
            *message = QStringLiteral("待定/放弃凭据含不应存在的提交修订号，禁止释放保护。");
            return false;
        }
        *finalRevision = -1;
        return true;
    }
    bool validFinal = false;
    *finalRevision = query.value(4).toLongLong(&validFinal);
    if (!validFinal || *finalRevision < 0 || *outcome != "committed" ||
        expected == std::numeric_limits<qint64>::max() || *finalRevision != expected + 1 ||
        (operation == "saveRack" && target <= 0))
    {
        *message = QStringLiteral("源事务结局损坏，禁止释放保护。");
        return false;
    }
    return true;
}

bool SampleStore::abortQualityOutcome(QString token, QString *outcome, QString *message)
{
    if (!sql(database_, "BEGIN IMMEDIATE", {}, message)) return false;
    qint64 finalRevision = -1;
    bool ok = readQualityOutcome(token, outcome, &finalRevision, message);
    if (ok && *outcome == "pending")
    {
        ok = sql(database_, "UPDATE sample_local_quality_change SET outcome='aborted',after_revision=NULL "
                "WHERE change_token=? AND owner_uuid=? AND quality_uuid=? AND outcome='pending'",
                {token, qualityOwnerId_, qualityTargetId_}, message, 1);
        if (ok) *outcome = "aborted";
    }
    // COMMITTED只读确认，绝不改成ABORTED。
    if (ok && database_.commit()) return true;
    if (ok) *message = QStringLiteral("放弃凭据提交未确认：") + database_.lastError().text();
    rollbackQualityTransaction(message);
    return false;
}

bool SampleStore::forgetQualityOutcome(QString token, QString *message)
{
    // 调用门槛：只有同token/owner/quality家族的QC实际对账ACK才能进入这里。
    if (!sql(database_, "BEGIN IMMEDIATE", {}, message)) return false;
    QString outcome;
    qint64 finalRevision = -1;
    bool ok = readQualityOutcome(token, &outcome, &finalRevision, message);
    if (ok && outcome == "pending")
    {
        ok = false;
        *message = QStringLiteral("待定源事务不能清理。");
    }
    if (ok)
    {
        const QString condition = outcome == "committed" ? "outcome='committed' AND after_revision=?"
                                                         : "outcome='aborted' AND after_revision IS NULL";
        QVariantList values{token, qualityOwnerId_, qualityTargetId_};
        if (outcome == "committed") values.append(finalRevision);
        ok = sql(database_, "DELETE FROM sample_local_quality_change WHERE change_token=? "
                 "AND owner_uuid=? AND quality_uuid=? AND " + condition, values, message, 1);
    }
    if (ok && database_.commit()) return true;
    if (ok) *message = QStringLiteral("源凭据清理未提交：") + database_.lastError().text();
    rollbackQualityTransaction(message);
    return false;
}

void SampleStore::finishAbortedQualityChange(QString reason)
{
    QString outcome, detail;
    // 只在原写事务已结束且凭据可重查后调用；COMMITTED永远保持已保存，不按方法名强制放弃。
    if (!abortQualityOutcome(pendingQuality_.change.changeToken, &outcome, &detail))
    {
        pendingQuality_.message = reason + QStringLiteral("；源放弃结局未持久确认，保护保留：") + detail;
        completeQualityChange(false, detail);
        return;
    }
    qint64 finalRevision = -1;
    if (!readQualityOutcome(pendingQuality_.change.changeToken, &outcome, &finalRevision, &detail))
    {
        pendingQuality_.message = reason + detail;
        completeQualityChange(false, detail);
        return;
    }
    pendingQuality_.committed = outcome == "committed";
    pendingQuality_.message = pendingQuality_.committed ? QString() : reason;
    pendingQuality_.observedRevision = finalRevision;
    pendingQuality_.stage = "finish";
    qualityReplyTimer_->start();
    emit finishQualityChange(pendingQuality_.change.changeToken, pendingQuality_.committed, finalRevision);
}

void SampleStore::startQualityChange(qint64 request, QString operation, SampleRack rack, qint64 targetId)
{
    if (!check(request, operation, true)) return;
    QString message;
    SampleSnapshot saved;
    qint64 expected = 0;
    if (!database_.transaction()) { emit error(request,operation,database_.lastError().text()); return; }
    bool ok = read(&saved,&message) && revision(database_,&expected,&message);
    if (!ok || !database_.commit())
    {
        if (message.isEmpty()) message = database_.lastError().text();
        rollbackQualityTransaction(&message);
        emit error(request, operation, message);
        return;
    }
    bool needsQuality = operation == "saveRack" && containsQuality(rack);
    for (const SampleRack &known : saved.racks)
        if (((operation == "clearOrdinaryRacks" && !known.emergency) ||
             (operation != "clearOrdinaryRacks" && known.id == targetId)) && containsQuality(known)) needsQuality = true;
    if (needsQuality && (!qualityBound_ || QUuid(qualityTargetId_).isNull()))
    { emit error(request,operation,QStringLiteral("质控引用尚未完成绑定核对，未修改预设。"));return; }
    pendingQuality_ = PendingQualityChange();
    pendingQuality_.request = request; pendingQuality_.operation = operation;
    pendingQuality_.rack = rack; pendingQuality_.targetId = targetId;
    pendingQuality_.needsQuality = needsQuality;
    auto &change = pendingQuality_.change;
    change.changeToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
    change.ownerDatasetId = qualityOwnerId_;
    change.qualityDatasetId = needsQuality ? qualityTargetId_ : QString();
    change.catalogCanonicalPath = QFileInfo(database_.databaseName()).canonicalFilePath();
    change.expectedRevision = expected;
    if (operation == "saveRack")
    {
        for (SampleHole &hole : pendingQuality_.rack.holes)
        {
            if (hole.sampleType != 2) continue;
            hole.quality.slotToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
            hole.quality.changeToken = change.changeToken;
            if (hole.quality.selected.datasetId != change.qualityDatasetId)
            {
                pendingQuality_.message = QStringLiteral("质控预设不属于已绑定的质控数据集，未保存。");
                completeQualityChange(false, pendingQuality_.message);
                return;
            }
            change.claims.append({hole.quality.slotToken,hole.quality.selected});
        }
    }
    if (needsQuality)
    {
        for (const SampleRack &known : saved.racks)
        {
            const bool affected = operation == "clearOrdinaryRacks" ? !known.emergency : known.id == targetId;
            if (!affected) continue;
            for (const SampleHole &hole : known.holes)
            {
                if (hole.sampleType == 2 && hole.quality.selected.datasetId != qualityTargetId_)
                {
                    pendingQuality_.message = QStringLiteral("已保存质控预设绑定另一数据集，未重绑定或删除。");
                    completeQualityChange(false, pendingQuality_.message);
                    return;
                }
            }
        }
        if (!persistPendingQualityChange(&message))
        {
            pendingQuality_.message = message;
            completeQualityChange(false, message);
            return;
        }
        ownQualityChanges_.insert(change.changeToken);
        pendingQuality_.stage = "reserve";
        qualityReplyTimer_->start();
        emit reserveQualityChange(change); // 此处没有持有catalog写锁。
    }
    else commitQualityChange();
}
void SampleStore::saveRack(qint64 request, SampleRack rack)
{
    if (originalRackChange(request, QStringLiteral("saveRack"), rack, rack.id)) return;
    startQualityChange(request,"saveRack",rack,rack.id);
}
void SampleStore::removeRack(qint64 request, qint64 id)
{
    if (originalRackChange(request, QStringLiteral("removeRack"), SampleRack(), id)) return;
    startQualityChange(request,"removeRack",SampleRack(),id);
}
void SampleStore::clearOrdinaryRacks(qint64 request)
{
    if (originalRackChange(request, QStringLiteral("clearOrdinaryRacks"), SampleRack(), 0)) return;
    startQualityChange(request,"clearOrdinaryRacks",SampleRack(),0);
}

void SampleStore::qualityChangeReserved(SampleQcReservationReply reply)
{
    if (QThread::currentThread() != thread()) return;
    const bool family = reply.ownerDatasetId == qualityOwnerId_ && reply.qualityDatasetId == qualityTargetId_;
    if (reply.changeToken == pendingQuality_.change.changeToken && pendingQuality_.stage == "finish") return;
    if (reply.changeToken != pendingQuality_.change.changeToken || pendingQuality_.stage != "reserve")
    {
        // 迟到预约不更新新草稿，也不能接管另一个写者仍活跃的pending。
        if (!family || !ownQualityChanges_.contains(reply.changeToken)) return;
        QString outcome, message;
        qint64 finalRevision = -1;
        if (!abortQualityOutcome(reply.changeToken, &outcome, &message) ||
            !readQualityOutcome(reply.changeToken, &outcome, &finalRevision, &message))
        {
            emit warning(0, "qualityReferences", QStringLiteral("迟到预约的结局未确认，保护保留：") + message);
            return;
        }
        emit finishQualityChange(reply.changeToken, outcome == "committed", finalRevision);
        return;
    }
    qualityReplyTimer_->stop();
    bool matching = reply.reserved && family &&
                    reply.claims.size() == pendingQuality_.change.claims.size();
    for (int i = 0; matching && i < reply.claims.size(); ++i)
        matching = reply.claims[i].slotToken == pendingQuality_.change.claims[i].slotToken &&
                   sameSampleQcSelection(reply.claims[i].selected,pendingQuality_.change.claims[i].selected);
    if (!matching || closeAfterQuality_)
    {
        const QString reason = closeAfterQuality_ ? QStringLiteral("关闭期间取消未提交的预设。") :
            reply.message.isEmpty() ? QStringLiteral("质控预约未确认或身份已变化，未保存。") : reply.message;
        finishAbortedQualityChange(reason);
        return;
    }
    commitQualityChange();
}

bool SampleStore::writeQualityRack(QString *message)
{
    SampleRack &rack = pendingQuality_.rack;
    *message = validateRack(rack);
    if (!message->isEmpty())
    {
        return false;
    }
    if (rack.id)
    {
        SampleSnapshot saved;
        if (!read(&saved, message, rack.id) || saved.racks.size() != 1)
        {
            if (message->isEmpty()) *message = QStringLiteral("预设已不存在。");
            return false;
        }
        // 身份字段也属于孔位内容，不能通过改类型绕过Testing/revise保护。
        for (const SampleHole &old : saved.racks.first().holes)
        {
            for (const SampleHole &draft : rack.holes)
            {
                const bool protectedHole = !old.revise ||
                    sampleHoleState(sampleHoleTaskStates(old)) == SampleHoleState::Testing;
                if (old.number == draft.number && protectedHole && !sameQualityHole(old, draft))
                {
                    *message = QStringLiteral("正在测试或不可修改孔位未被覆盖。");
                    return false;
                }
            }
        }
        if (!sql(database_, "UPDATE sample_local_rack SET name=? WHERE id=? AND emergency=?",
                 {rack.name, rack.id, rack.emergency}, message, 1))
        {
            return false;
        }
    }
    else
    {
        if (rack.emergency)
        {
            *message = QStringLiteral("不能新增第二个急诊架。");
            return false;
        }
        QSqlQuery add(database_);
        add.prepare("INSERT INTO sample_local_rack(name,emergency) VALUES(?,0)");
        add.addBindValue(rack.name);
        if (!add.exec())
        {
            *message = add.lastError().text();
            return false;
        }
        rack.id = add.lastInsertId().toLongLong();
    }
    // 原孔位/任务及新增QC身份一起替换；任何一项失败都由外层事务回滚。
    if (!sql(database_, "DELETE FROM sample_local_hole WHERE rack_id=?", {rack.id}, message))
    {
        return false;
    }
    for (const SampleHole &hole : rack.holes)
    {
        if (!writeQualityHole(rack.id, hole, message)) return false;
    }
    return true;
}

bool SampleStore::writeQualityHole(qint64 rackId, const SampleHole &hole, QString *message)
{
    const QString sampleId = hole.sampleId.isNull() ? QStringLiteral("") : hole.sampleId;
    if (!sql(database_, "INSERT INTO sample_local_hole VALUES(?,?,?,?)",
             {rackId, hole.number, sampleId, hole.container}, message) ||
        !sql(database_, "INSERT INTO sample_local_hole_state VALUES(?,?,?)",
             {rackId, hole.number, hole.revise}, message))
    {
        return false;
    }
    for (const SampleTask &task : hole.tasks)
    {
        if (!sql(database_, "INSERT INTO sample_local_task VALUES(?,?,?,?,?,?)",
                 {rackId, hole.number, task.projectId, task.rate, task.repeats, task.prolonged}, message))
        {
            return false;
        }
        if (!task.states.isEmpty())
        {
            QJsonArray states;
            for (SampleTaskState state : task.states) states.append(static_cast<int>(state));
            const QString text = QString::fromUtf8(QJsonDocument(states).toJson(QJsonDocument::Compact));
            if (!sql(database_, "INSERT INTO sample_local_task_state VALUES(?,?,?,?)",
                     {rackId, hole.number, task.projectId, text}, message))
            {
                return false;
            }
        }
    }
    if (hole.sampleType != 2) return true;
    const SampleQcIdentity &identity = hole.quality;
    const auto &selected = identity.selected;
    if (!sql(database_, "INSERT INTO sample_local_quality_hole VALUES(?,?,2,?,?,?,?,?,?,?)",
             {rackId, hole.number, selected.datasetId, selected.controlId, selected.batchId,
              selected.controlName, selected.batchName, identity.slotToken, identity.changeToken}, message))
    {
        return false;
    }
    for (int index = 0; index < selected.projects.size(); ++index)
    {
        const auto &project = selected.projects[index];
        if (!sql(database_, "INSERT INTO sample_local_quality_project VALUES(?,?,?,?,?)",
                 {rackId, hole.number, index, project.associationId, project.projectId}, message))
        {
            return false;
        }
    }
    return true;
}

bool SampleStore::sameQualityHole(const SampleHole &a,const SampleHole &b) const
{
    if(a.sampleType!=b.sampleType || !sameSampleQcSelection(a.quality.selected,b.quality.selected) ||
       a.sampleId!=b.sampleId || a.container!=b.container || a.revise!=b.revise || a.tasks.size()!=b.tasks.size())return false;
    for(const SampleTask &task:a.tasks)
    {
        bool found=false;
        for(const SampleTask &other:b.tasks)
            if(task.projectId==other.projectId)
                found=task.rate==other.rate && task.repeats==other.repeats && task.prolonged==other.prolonged && sampleTaskStates(task)==sampleTaskStates(other);
        if(!found)return false;
    }
    return true; // 预约token是恢复元数据，不把token变化误认为测试参数变化。
}

void SampleStore::commitQualityChange()
{
    QString message;
    bool ok = ready_ && canEdit(session_) && !closeAfterQuality_ &&
              (!pendingQuality_.needsQuality || qualityBound_);
    if (!ok) message = QStringLiteral("身份已变化或正在关闭，未提交预设。");

    // 先有QC持久预约，再取得catalog写锁；事务内不等待另一个worker。
    bool began = false;
    if (ok)
    {
        began = sql(database_, "BEGIN IMMEDIATE", {}, &message);
        ok = began;
    }
    if (ok && pendingQuality_.needsQuality)
    {
        QString outcome;
        qint64 finalRevision = -1;
        ok = readQualityOutcome(pendingQuality_.change.changeToken, &outcome, &finalRevision, &message);
        if (ok && outcome != "pending")
        {
            ok = false;
            message = QStringLiteral("源凭据已非待定，未重复提交预设。");
        }
        QSqlQuery marker(database_);
        marker.prepare("SELECT before_revision FROM sample_local_quality_change WHERE change_token=? "
                       "AND owner_uuid=? AND quality_uuid=? AND outcome='pending'");
        marker.addBindValue(pendingQuality_.change.changeToken);
        marker.addBindValue(qualityOwnerId_);
        marker.addBindValue(qualityTargetId_);
        if (ok && (!marker.exec() || !marker.next() ||
                   marker.value(0).toLongLong() != pendingQuality_.change.expectedRevision))
        {
            ok = false;
            message = QStringLiteral("源待定凭据的身份/修订号已变化，未提交预设。");
        }
    }
    qint64 observed = -1;
    if (ok) ok = revision(database_, &observed, &message);
    QString actualOwner;
    if (ok) ok = metadata(database_, "sample_dataset_uuid", &actualOwner, &message);
    if (ok && actualOwner != qualityOwnerId_)
    {
        ok = false;
        message = QStringLiteral("样本数据集身份已变化，未覆盖预设。");
    }
    if (ok && observed != pendingQuality_.change.expectedRevision)
    {
        ok = false;
        message = QStringLiteral("样本预设已被其他写者修改，请刷新后重试。");
    }
    if (ok)
    {
        ok = pendingQuality_.operation == "saveRack" ? writeQualityRack(&message)
                                                     : eraseQualityRacks(&message);
    }
    if (ok)
    {
        ok = sql(database_, "UPDATE catalog_metadata SET value=? WHERE key='sample_qc_revision' AND value=?",
                 {QString::number(observed + 1), QString::number(observed)}, &message, 1);
    }
    if (ok && pendingQuality_.needsQuality)
    {
        const qint64 target = pendingQuality_.operation == "saveRack" ? pendingQuality_.rack.id : pendingQuality_.targetId;
        ok = sql(database_, "UPDATE sample_local_quality_change SET outcome='committed',after_revision=?,target_rack_id=? "
                 "WHERE change_token=? AND owner_uuid=? AND quality_uuid=? "
                 "AND before_revision=? AND outcome='pending'",
                 {observed + 1, target, pendingQuality_.change.changeToken, qualityOwnerId_, qualityTargetId_,
                  pendingQuality_.change.expectedRevision}, &message, 1);
    }
    if (ok && !database_.commit())
    {
        ok = false;
        message = QStringLiteral("样本提交结局待核，原草稿和引用保护保留：") + database_.lastError().text();
    }
    if (!ok)
    {
        const bool outcomeResolvable = !began || rollbackQualityTransaction(&message, pendingQuality_.needsQuality);
        pendingQuality_.message = message;
        if (!outcomeResolvable)
        {
            // 未知结局绝不伪造ABORTED，也不持锁等待QC。数据库连接已关闭。
            completeQualityChange(false, message);
            return;
        }
        if (pendingQuality_.needsQuality)
        {
            finishAbortedQualityChange(message);
        }
        else
        {
            completeQualityChange(true, QString());
        }
        return;
    }
    pendingQuality_.committed = true;
    pendingQuality_.message.clear();
    pendingQuality_.observedRevision = observed + 1;
    if (pendingQuality_.needsQuality)
    {
        pendingQuality_.stage = "finish";
        qualityReplyTimer_->start();
        // catalog已提交/回滚，现在QC服务回读真实结局；不能只按bool释放保护。
        emit finishQualityChange(pendingQuality_.change.changeToken, true, pendingQuality_.observedRevision);
    }
    else
    {
        completeQualityChange(true, QString());
    }
}

bool SampleStore::eraseQualityRacks(QString *message)
{
    const bool oneRack = pendingQuality_.operation == "removeRack";
    SampleSnapshot saved;
    if (!read(&saved, message, oneRack ? pendingQuality_.targetId : 0)) return false;
    if (oneRack)
    {
        if (saved.racks.size() != 1 || saved.racks.first().emergency || locked(saved.racks.first()))
        {
            *message = QStringLiteral("目标不存在、急诊架或含不可修改孔位，未删除。");
            return false;
        }
        return sql(database_, "DELETE FROM sample_local_rack WHERE id=? AND emergency=0",
                   {pendingQuality_.targetId}, message, 1);
    }
    for (const SampleRack &rack : saved.racks)
    {
        if (!rack.emergency && locked(rack))
        {
            *message = QStringLiteral("普通架含不可修改孔位，整批未清空。");
            return false;
        }
    }
    return sql(database_, "DELETE FROM sample_local_rack WHERE emergency=0", {}, message);
}
void SampleStore::qualityChangeFinished(SampleQcFinishReply reply)
{
    if (QThread::currentThread() != thread() || reply.ownerDatasetId != qualityOwnerId_ ||
        reply.qualityDatasetId != qualityTargetId_) return;
    const bool current = reply.changeToken == pendingQuality_.change.changeToken && pendingQuality_.stage == "finish";
    QString outcome, message;
    qint64 finalRevision = -1;
    const bool terminal = readQualityOutcome(reply.changeToken, &outcome, &finalRevision, &message) &&
                          (outcome == "committed" || outcome == "aborted");
    if (current && terminal)
    {
        pendingQuality_.committed = outcome == "committed";
        pendingQuality_.observedRevision = finalRevision;
    }
    bool cleaned = false;
    if (reply.reconciled && terminal)
    {
        cleaned = forgetQualityOutcome(reply.changeToken, &message);
        if (cleaned) ownQualityChanges_.remove(reply.changeToken);
    }
    if (!current)
    {
        if (reply.reconciled && !cleaned)
            emit warning(0, "qualityReferences", QStringLiteral("迟到对账ACK的源凭据保留待核：") + message);
        return;
    }
    const QString detail = reply.message + (message.isEmpty() ? QString() : QStringLiteral("；") + message);
    completeQualityChange(reply.reconciled && cleaned, detail);
}
void SampleStore::completeQualityChange(bool reconciled,QString detail)
{
    qualityReplyTimer_->stop();
    const auto finished=pendingQuality_;pendingQuality_=PendingQualityChange();
    if(!reconciled)
    {
        qualityBound_=false;
        emit qualityProtectionReady(false);
    }
    if(finished.committed)
    {
        if(!reconciled)emit warning(finished.request,finished.operation,QStringLiteral("预设已保存，质控保护保留；清理待核：")+detail);
        emit succeeded(finished.request,finished.operation,finished.operation=="saveRack"?finished.rack.id:finished.targetId);
    }
    else emit error(finished.request,finished.operation,finished.message.isEmpty()?QStringLiteral("预设未保存；质控引用保留待核。")+detail:finished.message);
    if(closeAfterQuality_)close();
}
void SampleStore::qualityReplyTimedOut()
{
    // 等预约期间尚未写架，先持久ABORTED再请求QC确认；不能只发送false。
    if (pendingQuality_.stage == "reserve")
    {
        finishAbortedQualityChange(QStringLiteral("质控预约回执超时，架草稿未提交。"));
        return;
    }
    // 等最终ACK期间保留COMMITTED/ABORTED凭据，重启后按真实结局恢复。
    completeQualityChange(false,QStringLiteral("质控对账ACK超时，源凭据及保护保留待核。"));
}
