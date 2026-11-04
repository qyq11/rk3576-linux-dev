#include "original_task_reagent_receipt_writer.h"
#include "projects/original_project_repository.h"
#include <QCryptographicHash>
#include <QDir>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>
#include <cmath>
#include <utility>

namespace OriginalTaskReagents
{
namespace
{
const QString settingFile=QStringLiteral("db/userSet/reagent_Setting.db");
const QString informationFile=QStringLiteral("db/userSet/reagent_Information.db");
const QString bottleFile=QStringLiteral("db/systemInfo/reagent_Bottle_Type.db");

bool fail(QString *message, const QString &text)
{
    if (message) *message=text;
    return false;
}
QString quoted(QString name)
{
    name.replace(QLatin1Char('"'),QStringLiteral("\"\""));
    return QLatin1Char('"')+name+QLatin1Char('"');
}
bool sameValue(const QVariant &a,const QVariant &b)
{
    return a.userType()==b.userType() && a.isNull()==b.isNull() && a==b;
}
bool versions(const QString &root,QMap<QString,QString> *out,QString *message)
{
    QMap<QString,QString> read;
    for (const auto &file : {informationFile,bottleFile,settingFile})
    {
        const QString value=OriginalProjectRepository::databaseFingerprint(QDir(root).filePath(file),message);
        if (value.isEmpty()) return false;
        read.insert(file,value);
    }
    *out=read;
    return true;
}
bool readPlate(QSqlDatabase database,const QString &schema,const QString &table,TypedPlate *out,QString *message)
{
    *out=TypedPlate();
    if ((schema!=QStringLiteral("main") && schema!=QStringLiteral("original_reagents")) ||
        (table!=QStringLiteral("reagent_setting") && table!=QStringLiteral("reagent_setting_6600")))
        return fail(message,QStringLiteral("任务凭据目标schema/孔表不属于原已选择表。"));
    const QString target=quoted(schema)+QLatin1Char('.')+quoted(table);
    QSqlQuery metadata(database);
    if (!metadata.exec(QStringLiteral("SELECT * FROM ")+target+QStringLiteral(" LIMIT 0")))
        return fail(message,metadata.lastError().text());
    QStringList columns,expressions;
    const QSqlRecord record=metadata.record();
    for (int i=0;i<record.count();++i)
    {
        const QString name=record.fieldName(i);
        if (name.isEmpty() || name.contains(QChar::Null) || columns.contains(name))
            return fail(message,QStringLiteral("任务凭据原列名重复或无效，未伪造完整行。"));
        columns.append(name); expressions.append(quoted(name));
    }
    metadata.finish();
    if (!columns.contains(QStringLiteral("id")) || !columns.contains(QStringLiteral("hole_name")) ||
        !columns.contains(QStringLiteral("used_ul")))
        return fail(message,QStringLiteral("任务凭据缺少原id/孔名/used列。"));
    for (const auto &column : columns) expressions.append(QStringLiteral("typeof(")+quoted(column)+QLatin1Char(')'));
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("SELECT ")+expressions.join(QLatin1Char(','))+QStringLiteral(" FROM ")+target+QStringLiteral(" ORDER BY id")))
        return fail(message,query.lastError().text());
    TypedPlate read; read.table=table; read.columns=columns;
    const QSet<QString> storageTypes{QStringLiteral("null"),QStringLiteral("integer"),QStringLiteral("real"),QStringLiteral("text"),QStringLiteral("blob")};
    while (query.next())
    {
        if (read.rows.size()>=100000)
            return fail(message,QStringLiteral("任务凭据原行超过既有10万行读取边界，未截断为完整快照。"));
        TypedRow row;
        for (int i=0;i<columns.size();++i)
        {
            const QString type=query.value(i+columns.size()).toString();
            if (!storageTypes.contains(type)) return fail(message,QStringLiteral("无法确认原列实际SQLite存储类型。"));
            row.values.insert(columns[i],query.value(i)); row.sqliteTypes.insert(columns[i],type);
        }
        read.rows.append(row);
    }
    if (query.lastError().isValid()) return fail(message,query.lastError().text());
    read.complete=true; *out=read;
    return true;
}
bool indexPlate(const TypedPlate &plate,QMap<qint64,const TypedRow *> *indexed,QString *message)
{
    indexed->clear(); QSet<QString> names;
    if (!plate.complete) return fail(message,QStringLiteral("任务凭据没有完整原孔行。"));
    for (const auto &row : plate.rows)
    {
        bool ok=false;
        const qint64 id=row.values.value(QStringLiteral("id")).toLongLong(&ok);
        const QString hole=row.values.value(QStringLiteral("hole_name")).toString();
        if (!ok || hole.isEmpty() || indexed->contains(id) || names.contains(hole))
            return fail(message,QStringLiteral("任务凭据原孔id/孔名不唯一，未猜测行身份。"));
        indexed->insert(id,&row); names.insert(hole);
    }
    return true;
}
bool observedMatches(const TypedPlate &plate,const OriginalReagentState &observed,QString *message)
{
    QMap<qint64,const TypedRow *> indexed;
    if (!indexPlate(plate,&indexed,message) || plate.rows.size()!=observed.holes.size())
        return fail(message,QStringLiteral("事务内完整孔行不对应分配时的原孔清单。"));
    for (const auto &row : observed.holes)
    {
        const auto *actual=indexed.value(row.value(QStringLiteral("id")).toLongLong(),nullptr);
        if (!actual) return fail(message,QStringLiteral("事务内缺少分配时的原孔。"));
        for (auto field=row.cbegin();field!=row.cend();++field)
            if (!actual->values.contains(field.key()) || !sameValue(actual->values.value(field.key()),field.value()))
                return fail(message,QStringLiteral("事务内原行/类型不同于已分配的原值，运行凭据未核实。"));
    }
    return true;
}
bool plateTransition(const Commit &commit,const TypedPlate &after,QString *message)
{
    const auto &before=commit.beforePlate;
    if (!before.complete || !after.complete || before.table!=after.table || before.columns!=after.columns || before.rows.size()!=after.rows.size())
        return fail(message,QStringLiteral("任务提交前后完整表列或行集合不同。"));
    QMap<qint64,const TypedRow *> first,last;
    if (!indexPlate(before,&first,message) || !indexPlate(after,&last,message)) return false;
    QMap<qint64,SqlHoleWrite> writes;
    for (const auto &write : commit.sqlWrites)
    {
        if (writes.contains(write.rowId) || write.columns!=QStringList{QStringLiteral("used_ul")})
            return fail(message,QStringLiteral("任务凭据的SQL孔或允许改列重复/越界。"));
        writes.insert(write.rowId,write);
    }
    for (auto item=first.cbegin();item!=first.cend();++item)
    {
        const auto *saved=last.value(item.key(),nullptr);
        if (!saved || item.value()->values.keys()!=saved->values.keys())
            return fail(message,QStringLiteral("任务提交后孔行身份集合改变。"));
        const bool written=writes.contains(item.key());
        for (const auto &column : before.columns)
        {
            if (written && column==QStringLiteral("used_ul")) continue;
            if (!sameValue(item.value()->values.value(column),saved->values.value(column)) ||
                item.value()->sqliteTypes.value(column)!=saved->sqliteTypes.value(column))
                return fail(message,QStringLiteral("任务提交改变了其他孔或不属于used的原列。"));
        }
        if (written)
        {
            const auto &write=writes.value(item.key());
            const auto &old=item.value()->values.value(QStringLiteral("used_ul"));
            if (item.value()->values.value(QStringLiteral("hole_name")).toString()!=write.hole ||
                !sameValue(old,write.predicateUsed))
                return fail(message,QStringLiteral("任务实际used前态或孔身份未对应本次SQL。"));
            // writtenUsed保留真实绑定；实际亲和转换以事务内typeof/值为准，不猜必为TEXT。
        }
    }
    for (auto write=writes.cbegin();write!=writes.cend();++write)
        if (!first.contains(write.key())) return fail(message,QStringLiteral("任务SQL孔不在完整前态中。"));
    return true;
}
bool samePlate(const TypedPlate &first,const TypedPlate &second,QString *message)
{
    if (!first.complete || !second.complete || first.table!=second.table || first.columns!=second.columns ||
        first.rows.size()!=second.rows.size())
        return fail(message,QStringLiteral("事务内写后与COMMIT后完整孔行集合不同。"));
    QMap<qint64,const TypedRow *> before,after;
    if (!indexPlate(first,&before,message) || !indexPlate(second,&after,message)) return false;
    for (auto row=before.cbegin();row!=before.cend();++row)
    {
        const auto *current=after.value(row.key(),nullptr);
        if (!current || row.value()->values.keys()!=current->values.keys())
            return fail(message,QStringLiteral("COMMIT前后原孔身份/列集合不同。"));
        for (const auto &column : first.columns)
            if (!sameValue(row.value()->values.value(column),current->values.value(column)) ||
                row.value()->sqliteTypes.value(column)!=current->sqliteTypes.value(column))
                return fail(message,QStringLiteral("COMMIT后的原值或typeof不同于本事务实际写后值。"));
    }
    return true;
}
bool basisMatches(const Commit &commit)
{
    const auto &basis=commit.basis;
    const QString table=basis.configuredPositions==20 ? QStringLiteral("reagent_setting") :
        basis.configuredPositions==42 ? QStringLiteral("reagent_setting_6600") : QString();
    if (!basis.available || !basis.actionId || !taskOriginAllows(basis,commit.action) || !basis.connection || !basis.basisRevision ||
        basis.basisToken.isEmpty() || basis.root!=commit.root || basis.payloadSha256!=commit.payloadSha256 || basis.originDigest!=commit.originDigest ||
        basis.versions!=commit.beforeVersions || table!=commit.settingTable || basis.holes.isEmpty()) return false;
    QMap<qint64,const TypedRow *> indexed; QString ignored;
    if (!indexPlate(commit.beforePlate,&indexed,&ignored)) return false;
    bool trusted=false;
    for (auto entry=basis.holes.cbegin();entry!=basis.holes.cend();++entry)
    {
        const auto &hole=entry.value();
        if (!hole.available) continue;
        const auto *row=indexed.value(hole.rowId,nullptr);
        if (!row || hole.hole!=entry.key() || !hole.revision || hole.originalIdentity.isEmpty() ||
            !hole.originalIdentity.contains(QStringLiteral("id")) || !hole.originalIdentity.contains(QStringLiteral("hole_name")) ||
            row->values.value(QStringLiteral("hole_name")).toString()!=hole.hole) return false;
        for (auto field=hole.originalIdentity.cbegin();field!=hole.originalIdentity.cend();++field)
            if (!row->values.contains(field.key()) || !sameValue(row->values.value(field.key()),field.value())) return false;
        trusted=true;
    }
    return trusted;
}
}

TaskOrigin taskOrigin(const OriginalTasks::Task &task)
{
    TaskOrigin origin;
    origin.taskId=task.taskId; origin.reportRow=task.reportRow; origin.projectId=task.project.id;
    origin.calibrationId=task.calibrationId; origin.rack=task.rack; origin.type=task.type;
    origin.projectName=task.project.name; origin.projectFile=task.project.originalFileName;
    origin.projectFingerprint=task.project.originalFingerprint; origin.hole=task.hole;
    origin.repeatIndex=task.repeatIndex; origin.calibrationPoint=task.calibrationPoint;
    origin.reagentSlots=task.reagentSlots; origin.reservedByHole=task.reservedReagentVolumes;
    return origin; // 不复制Task中的样本条码/患者或认证内容。
}

ReceiptWriter::ReceiptWriter(Action action,const QString &root,const QByteArray &payload,const RuntimeBasis &basis,
    Commit *result,CommitHandler committed)
    : result_(result), committed_(std::move(committed)), enabled_(result_ || bool(committed_))
{
    data_.receiptId=QUuid::createUuid().toString(QUuid::WithoutBraces);
    data_.action=action; data_.root=root; data_.basis=basis;
    if (basis.source==OperationSource::DevicePacket)
    {
        data_.payloadSha256=QString::fromLatin1(QCryptographicHash::hash(payload,QCryptographicHash::Sha256).toHex());
        data_.originDigest=data_.payloadSha256;
    }
    else
    {
        data_.originDigest=userOperationDigest(basis.userOperation);
        if (!payload.isEmpty() || !taskOriginAllows(basis,action))
        {
            data_.originDigest.clear(); // 保留事实，但绝不能采用这份不合法的来源证明。
            appendProblem(QStringLiteral("用户来源携带设备字节或不属于获准动作，凭据不能作为可信前版。"));
        }
    }
    if (result_) *result_=data_;
}
ReceiptWriter::~ReceiptWriter()
{
    finish();
}
void ReceiptWriter::appendProblem(const QString &message)
{
    if (message.isEmpty()) return;
    if (!data_.proofMessage.isEmpty()) data_.proofMessage+=QLatin1Char('\n');
    data_.proofMessage+=message;
}
void ReceiptWriter::transactionStarted()
{
    data_.transaction=TransactionState::NotCommitted; // 尚无成功COMMIT，不声称回滚已独立核实。
}
void ReceiptWriter::captureBefore(QSqlDatabase database,const OriginalReagentState &observed)
{
    data_.settingTable=observed.settingTable; data_.beforeVersions=observed.versions;
    if (!enabled_) return;
    QString message; QMap<QString,QString> current;
    beforeVerified_=versions(data_.root,&current,&message) && current==observed.versions &&
        readPlate(database,QStringLiteral("original_reagents"),observed.settingTable,&data_.beforePlate,&message) &&
        observedMatches(data_.beforePlate,observed,&message);
    if (!beforeVerified_) appendProblem(QStringLiteral("SQL前态凭据未核实：")+(message.isEmpty() ? QStringLiteral("原三库版本不匹配。") : message));
    data_.basisMatchesDatabaseBefore=beforeVerified_ && basisMatches(data_);
}
void ReceiptWriter::matchedUsedWrite(const OriginalBusiness::Row &before,const QString &usedText)
{
    SqlHoleWrite write;
    write.rowId=before.value(QStringLiteral("id")).toLongLong();
    write.hole=before.value(QStringLiteral("hole_name")).toString();
    write.predicateUsed=before.value(QStringLiteral("used_ul")); write.writtenUsed=usedText;
    write.columns=QStringList{QStringLiteral("used_ul")};
    data_.sqlWrites.append(write); data_.reagentSqlWritten=true;
}
void ReceiptWriter::captureTransactionAfter(QSqlDatabase database)
{
    if (!enabled_) return;
    QString message;
    transactionAfterVerified_=readPlate(database,QStringLiteral("original_reagents"),data_.settingTable,&transactionAfter_,&message) &&
        beforeVerified_ && plateTransition(data_,transactionAfter_,&message);
    if (!transactionAfterVerified_) appendProblem(QStringLiteral("事务内实际写后值/typeof或列范围未核实：")+message);
}
void ReceiptWriter::commitFailed()
{
    data_.transaction=TransactionState::CommitUnconfirmed;
    appendProblem(QStringLiteral("原SQLite COMMIT返回失败，未将其包装为已提交或已确认回滚；禁止据此重放。"));
}
void ReceiptWriter::commitSeen()
{
    data_.committed=true; data_.transaction=TransactionState::Committed;
    if (result_) *result_=data_; // 第一时间保留真实提交；后核和内存更新不能抹掉。
}
void ReceiptWriter::verifyAfter()
{
    QString message; QMap<QString,QString> beforeRead,afterRead;
    bool read=versions(data_.root,&beforeRead,&message);
    if (read)
    {
        OriginalBusiness::Database database;
        read=database.open(data_.root,settingFile,false,&message) &&
            readPlate(database.db,QStringLiteral("main"),data_.settingTable,&data_.afterPlate,&message);
    }
    read=read && versions(data_.root,&afterRead,&message) && beforeRead==afterRead;
    if (read) data_.afterVersions=afterRead;
    data_.postCommitVerified=read && beforeVerified_ && transactionAfterVerified_ && data_.beforeVersions.size()==3 &&
        afterRead.value(informationFile)==data_.beforeVersions.value(informationFile) &&
        afterRead.value(bottleFile)==data_.beforeVersions.value(bottleFile) && samePlate(transactionAfter_,data_.afterPlate,&message);
    if (!data_.postCommitVerified)
        appendProblem(QStringLiteral("已提交，但锁内完整after/三库/列范围未核实：")+
            (message.isEmpty() ? QStringLiteral("读取版本变化或其他依赖未匹配。") : message));
    if (!data_.basisMatchesDatabaseBefore)
        appendProblem(QStringLiteral("已提交事实保留，但没有与库before匹配的冻结basis；不证明GUI当前可采用。"));
    if (!data_.afterPlate.complete) return;
    for (const auto &row : data_.afterPlate.rows)
    {
        RefreshHole refresh;
        refresh.rowId=row.values.value(QStringLiteral("id")).toLongLong();
        refresh.hole=row.values.value(QStringLiteral("hole_name")).toString();
        refresh.storedUsed=row.values.value(QStringLiteral("used_ul"));
        refresh.sqliteType=row.sqliteTypes.value(QStringLiteral("used_ul"));
        const auto basis=data_.basis.holes.constFind(refresh.hole);
        refresh.trustedBasis=data_.basisMatchesDatabaseBefore && basis!=data_.basis.holes.cend() && basis->available && basis->rowId==refresh.rowId;
        const QString text=refresh.storedUsed.toString();
        bool ok=false;
        if (!refresh.storedUsed.isNull() && refresh.sqliteType!=QStringLiteral("blob"))
        {
            const double used=text.isEmpty() ? 0.0 : text.toDouble(&ok);
            if (text.isEmpty()) ok=true;
            if (ok && std::isfinite(used)) { refresh.usedMicrolitres=used; refresh.displayConversionVerified=true; }
        }
        if (!refresh.displayConversionVerified) refresh.message=QStringLiteral("原NULL/非数值/类型不适合证明正常显示值；原值与typeof已保留，未补0成功。" );
        else if (text.isEmpty()) refresh.message=QStringLiteral("原空文本按控件读取转换为0，不是SQL写入正常0或物理液量证明。" );
        data_.refreshHoles.append(refresh);
    }
}
void ReceiptWriter::finish()
{
    if (finished_) return;
    finished_=true;
    if (data_.committed && enabled_) verifyAfter();
    if (result_) *result_=data_;
    if (data_.committed && committed_) committed_(data_);
}
}
