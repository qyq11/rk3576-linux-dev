#include "original_task_reagent_display.h"
#include <QSet>
#include <cmath>

namespace OriginalTaskReagents
{
namespace
{
bool fail(QString *message,const QString &reason)
{ if (message) *message=reason; return false; }

bool same(const QVariant &a,const QVariant &b)
{ return a.userType()==b.userType() && a.isNull()==b.isNull() && a==b; }

bool index(const TypedPlate &plate,QMap<qint64,const TypedRow *> *rows,QString *message)
{
    if (!plate.complete || plate.columns.isEmpty() ||
        (plate.table!=QStringLiteral("reagent_setting") && plate.table!=QStringLiteral("reagent_setting_6600")))
        return fail(message,QStringLiteral("任务凭据没有原孔表的完整行。"));
    QSet<QString> columns,names;
    for (const auto &column : plate.columns)
    {
        if (column.isEmpty() || columns.contains(column))
            return fail(message,QStringLiteral("任务凭据原列清单重复或为空。"));
        columns.insert(column);
    }
    for (const auto &key : {QStringLiteral("id"),QStringLiteral("hole_name"),QStringLiteral("used_ul")})
        if (!columns.contains(key)) return fail(message,QStringLiteral("任务凭据缺少原孔身份或used列。"));
    QStringList sorted=plate.columns; sorted.sort();
    const QSet<QString> types{QStringLiteral("null"),QStringLiteral("integer"),QStringLiteral("real"),QStringLiteral("text"),QStringLiteral("blob")};
    for (const auto &row : plate.rows)
    {
        if (row.values.keys()!=sorted || row.sqliteTypes.keys()!=sorted)
            return fail(message,QStringLiteral("任务凭据未逐列保留原值与SQLite类型。"));
        for (const auto &column : plate.columns)
            if (!types.contains(row.sqliteTypes.value(column)) ||
                (row.sqliteTypes.value(column)==QStringLiteral("null"))!=row.values.value(column).isNull())
                return fail(message,QStringLiteral("任务凭据原值的NULL或存储类型无法核实。"));
        bool ok=false;
        const qint64 id=row.values.value(QStringLiteral("id")).toLongLong(&ok);
        const QString hole=row.values.value(QStringLiteral("hole_name")).toString();
        if (!ok || id<=0 || hole.isEmpty() || rows->contains(id) || names.contains(hole))
            return fail(message,QStringLiteral("任务凭据孔id或孔名不唯一。"));
        rows->insert(id,&row); names.insert(hole);
    }
    return true;
}
}

bool verifyDisplayCommit(const Commit &receipt,QString *message)
{
    if (message) message->clear();
    const auto &basis=receipt.basis;
    if (!receipt.committed || receipt.transaction!=TransactionState::Committed ||
        !receipt.postCommitVerified || !receipt.basisMatchesDatabaseBefore || !basis.available ||
        receipt.receiptId.isEmpty() || !basis.actionId || !taskOriginAllows(basis,receipt.action) || !basis.connection ||
        !basis.permissionRevision || !basis.basisRevision || basis.basisToken.isEmpty() ||
        basis.root.isEmpty() || receipt.root!=basis.root || receipt.originDigest!=basis.originDigest ||
        receipt.payloadSha256!=basis.payloadSha256 ||
        (receipt.action!=Action::RackReservation && receipt.action!=Action::UnusedReservationReturn))
        return fail(message,QStringLiteral("任务提交事实、完整后核或动作前依据未核实；保留真实提交状态，显示待核。"));
    const QString information=QStringLiteral("db/userSet/reagent_Information.db");
    const QString bottle=QStringLiteral("db/systemInfo/reagent_Bottle_Type.db");
    const QString setting=QStringLiteral("db/userSet/reagent_Setting.db");
    if (receipt.beforeVersions.size()!=3 || receipt.afterVersions.size()!=3 || receipt.beforeVersions!=basis.versions)
        return fail(message,QStringLiteral("任务凭据三库前版与冻结依据不同。"));
    for (const auto &file : {information,bottle,setting})
        if (receipt.beforeVersions.value(file).isEmpty() || receipt.afterVersions.value(file).isEmpty())
            return fail(message,QStringLiteral("任务凭据缺少真实三库版本。"));
    if (receipt.beforeVersions.value(information)!=receipt.afterVersions.value(information) ||
        receipt.beforeVersions.value(bottle)!=receipt.afterVersions.value(bottle))
        return fail(message,QStringLiteral("任务期间试剂定义或瓶型库变化，不能续接旧最大量来源。"));
    const auto &before=receipt.beforePlate; const auto &after=receipt.afterPlate;
    const QString expectedTable=basis.configuredPositions==20 ? QStringLiteral("reagent_setting") :
        basis.configuredPositions==42 ? QStringLiteral("reagent_setting_6600") : QString();
    if (expectedTable.isEmpty() || receipt.settingTable!=expectedTable || before.table!=receipt.settingTable ||
        after.table!=before.table || before.columns!=after.columns || before.rows.size()!=after.rows.size())
        return fail(message,QStringLiteral("任务前后孔表、列或行集合不对应原配置。"));
    QMap<qint64,const TypedRow *> first,last;
    if (!index(before,&first,message) || !index(after,&last,message) || first.keys()!=last.keys()) return false;
    QMap<qint64,const SqlHoleWrite *> writes;
    for (const auto &write : receipt.sqlWrites)
    {
        const auto *old=first.value(write.rowId,nullptr);
        if (!old || writes.contains(write.rowId) || write.columns!=QStringList{QStringLiteral("used_ul")} ||
            write.hole!=old->values.value(QStringLiteral("hole_name")).toString() ||
            !same(write.predicateUsed,old->values.value(QStringLiteral("used_ul"))))
            return fail(message,QStringLiteral("任务SQL写孔、原WHERE值或列范围不对应完整前行。"));
        writes.insert(write.rowId,&write);
    }
    if (receipt.reagentSqlWritten!=!writes.isEmpty())
        return fail(message,QStringLiteral("任务SQL是否写入与实际写孔清单不同。"));
    for (auto row=first.cbegin();row!=first.cend();++row)
        for (const auto &column : before.columns)
        {
            if (column==QStringLiteral("used_ul") && writes.contains(row.key())) continue;
            const auto *saved=last.value(row.key());
            if (!same(row.value()->values.value(column),saved->values.value(column)) ||
                row.value()->sqliteTypes.value(column)!=saved->sqliteTypes.value(column))
                return fail(message,QStringLiteral("任务前后存在used允许写孔之外的字段变化。"));
        }
    // 不重演writer或SQL亲和转换。真实绑定由sqlWrites保存，实际after由完整typed链和三库后核绑定。
    QSet<qint64> refreshed;
    if (receipt.refreshHoles.size()!=after.rows.size() || basis.holes.size()!=before.rows.size())
        return fail(message,QStringLiteral("任务运行刷新清单或冻结孔集合不是整盘。"));
    for (const auto &refresh : receipt.refreshHoles)
    {
        const auto *saved=last.value(refresh.rowId,nullptr);
        if (!saved || refreshed.contains(refresh.rowId) ||
            saved->values.value(QStringLiteral("hole_name")).toString()!=refresh.hole ||
            !same(saved->values.value(QStringLiteral("used_ul")),refresh.storedUsed) ||
            saved->sqliteTypes.value(QStringLiteral("used_ul"))!=refresh.sqliteType)
            return fail(message,QStringLiteral("任务运行刷新孔不对应同次整盘after原值/类型。"));
        refreshed.insert(refresh.rowId);
        const auto found=basis.holes.constFind(refresh.hole);
        const auto *old=first.value(refresh.rowId,nullptr);
        if (found==basis.holes.cend() || found->rowId!=refresh.rowId || found->hole!=refresh.hole ||
            !old || found->originalIdentity.isEmpty() || refresh.trustedBasis!=found->available)
            return fail(message,QStringLiteral("任务整盘刷新与冻结孔身份不对应。"));
        for (auto field=found->originalIdentity.cbegin();field!=found->originalIdentity.cend();++field)
            if (!old->values.contains(field.key()) || !same(field.value(),old->values.value(field.key())))
                return fail(message,QStringLiteral("任务冻结孔的原字段/类型与完整前行不同。"));
        if (refresh.displayConversionVerified)
        {
            const QString text=refresh.storedUsed.toString(); bool ok=false;
            const double value=text.isEmpty() ? 0 : text.toDouble(&ok);
            if (refresh.storedUsed.isNull() || refresh.sqliteType==QStringLiteral("blob") ||
                (!text.isEmpty() && !ok) || !std::isfinite(value) || value!=refresh.usedMicrolitres)
                return fail(message,QStringLiteral("任务used显示转换与实际after原文不同，未用默认0补成功。"));
        }
    }
    return true;
}
}
