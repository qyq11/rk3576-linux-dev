#include "frmmain.h"
#include "task_reagent_context.h"
#include "instrument/device_control.h"
#include "reagents/reagents_page.h"
#include "reagents/original_reagent_level_worker.h"
#include "samples/sample_page.h"
#include <QCryptographicHash>
#include <QDir>
#include <QMetaType>
#include <QScopedValueRollback>
#include <QTimer>
#include <cmath>
#include <functional>
#include <limits>

namespace
{
using namespace TaskReagentRouting;
using OriginalLevels::CurrentHoleRuntime;
using OriginalLevels::MaximumSource;
using OriginalTaskReagents::TaskIdentity;

bool fail(QString *message,const QString &reason)
{ if (message) *message=reason; return false; }

QString capacityState(OriginalRuntime::ReagentCapacityFileState state)
{
    switch (state)
    {
    case OriginalRuntime::ReagentCapacityFileState::Present: return QStringLiteral("Present");
    case OriginalRuntime::ReagentCapacityFileState::Missing: return QStringLiteral("Missing");
    case OriginalRuntime::ReagentCapacityFileState::Unknown: return QString();
    }
    return QString();
}

bool sameIdentity(const TaskIdentity &a,const TaskIdentity &b)
{
    // revision由这些真实字段和账户身份的变化推进，不把Qj新鲜度或检测状态混进来。
    return a.available==b.available && a.connection==b.connection && a.root==b.root &&
        a.actualModel==b.actualModel && a.configuredModel==b.configuredModel &&
        a.protocolToken==b.protocolToken && a.themeFingerprint==b.themeFingerprint &&
        a.capacityRelativeFile==b.capacityRelativeFile && a.capacityFingerprint==b.capacityFingerprint &&
        a.capacityFileState==b.capacityFileState && a.configuredPositions==b.configuredPositions &&
        a.reportedPositions==b.reportedPositions && a.capacityCalibrationActive==b.capacityCalibrationActive;
}

bool observationMatches(const OriginalLevels::Context &context,const TaskIdentity &identity)
{
    return identity.available && context.connection==identity.connection && context.root==identity.root &&
        context.actualModel==identity.actualModel && context.configuredModel==identity.configuredModel &&
        context.protocolToken==identity.protocolToken && context.linuxNew==(identity.protocolToken==QStringLiteral("LinuxNew")) &&
        context.themeFingerprint==identity.themeFingerprint && context.capacityRelativeFile==identity.capacityRelativeFile &&
        context.capacityFingerprint==identity.capacityFingerprint && capacityState(context.capacityFileState)==identity.capacityFileState &&
        context.configuredPositions==identity.configuredPositions && context.reportedPositions==identity.reportedPositions &&
        context.capacityCalibrationActive==identity.capacityCalibrationActive;
}

struct SnapshotSource
{
    QMap<QString,const ReagentHole *> holes;
    QString setting,bottle,information;
};

bool collect(const QString &fingerprint,QString *version)
{
    if (fingerprint.isEmpty() || (!version->isEmpty() && *version!=fingerprint)) return false;
    *version=fingerprint;
    return true;
}

bool inspectSnapshot(const ReagentSnapshot &snapshot,SnapshotSource *source,QString *message)
{
    *source=SnapshotSource();
    if (!snapshot.originalProfile || snapshot.originalRoot.isEmpty())
        return fail(message,QStringLiteral("页面不是已读回的原试剂资料。"));
    const QStringList required{QStringLiteral("id"),QStringLiteral("hole_name"),QStringLiteral("area"),
        QStringLiteral("reagent_type"),QStringLiteral("reagent_name"),QStringLiteral("reagent_batch"),
        QStringLiteral("bottle_type"),QStringLiteral("amount_uL"),QStringLiteral("used_ul"),QStringLiteral("state"),
        QStringLiteral("priority"),QStringLiteral("datetime"),QStringLiteral("out_date"),QStringLiteral("formula_level"),
        QStringLiteral("out_ul"),QStringLiteral("warning_ul"),QStringLiteral("max_ul")};
    QSet<qint64> ids;
    for (const auto &hole : snapshot.holes)
    {
        const auto &row=hole.originalValues;
        bool idOk=false;
        const qint64 id=row.value(QStringLiteral("id")).toLongLong(&idOk);
        if (!idOk || id<=0 || id!=hole.id || ids.contains(id) || hole.name.isEmpty() ||
            row.value(QStringLiteral("hole_name")).toString()!=hole.name || source->holes.contains(hole.name) ||
            !collect(hole.originalFingerprint,&source->setting))
            return fail(message,QStringLiteral("当前孔位原id、孔名、唯一性或孔库版本不完整。"));
        for (const auto &key : required)
            if (!row.contains(key)) return fail(message,QStringLiteral("当前原孔位缺少字段：")+key);
        ids.insert(id); source->holes.insert(hole.name,&hole);
    }
    for (const auto &bottle : snapshot.bottles)
        if (bottle.originalValues.isEmpty() || !collect(bottle.originalFingerprint,&source->bottle))
            return fail(message,QStringLiteral("当前原瓶型行或瓶型库版本不完整。"));
    for (const auto &reagent : snapshot.reagents)
        if (reagent.originalValues.isEmpty() || !collect(reagent.originalFingerprint,&source->information))
            return fail(message,QStringLiteral("当前原试剂行或试剂库版本不完整。"));
    for (const auto &batch : snapshot.batches)
        if (!collect(batch.originalFingerprint,&source->information))
            return fail(message,QStringLiteral("当前原批组与试剂库版本不一致。"));
    for (const auto &barcode : snapshot.barcodes)
        if (!collect(barcode.originalFingerprint,&source->information))
            return fail(message,QStringLiteral("当前原条码与试剂库版本不一致。"));
    if (source->setting.isEmpty() || source->bottle.isEmpty() || source->information.isEmpty())
        return fail(message,QStringLiteral("当前快照不足以证明孔位、瓶型和试剂三库版本。"));
    return true;
}

bool rawText(const QVariantMap &row,const QString &key,QString *value)
{
    const auto field=row.constFind(key);
    if (field==row.cend() || !field->isValid() || field->isNull() || field->userType()==QMetaType::QByteArray) return false;
    *value=field->toString();
    return true;
}

bool displayNumber(const QVariant &raw,double *number)
{
    if (!raw.isValid() || raw.isNull() || raw.userType()==QMetaType::QByteArray) return false;
    const QString text=raw.toString();
    if (text.isEmpty())
    {
        if (raw.userType()!=QMetaType::QString) return false;
        *number=0; // 仅原非NULL空文本的显示转换；不替Ur空used保旧值的规则。
        return true;
    }
    bool ok=false;
    *number=text.toDouble(&ok);
    return ok && std::isfinite(*number);
}

bool sameRuntimeIdentity(const CurrentHoleRuntime &runtime,const ReagentSnapshot &snapshot,
                         const ReagentHole &hole,const SnapshotSource &source)
{
    const auto &row=hole.originalValues;
    const QStringList keys{QStringLiteral("hole_name"),QStringLiteral("reagent_name"),QStringLiteral("bottle_type"),
        QStringLiteral("area"),QStringLiteral("reagent_type"),QStringLiteral("state")};
    QStringList values;
    for (const auto &key : keys)
    {
        QString value;
        if (!rawText(row,key,&value)) return false;
        values.append(value);
    }
    return runtime.root==snapshot.originalRoot && runtime.hole==hole.name && runtime.hole==values[0] &&
        runtime.reagent==values[1] && runtime.bottle==values[2] && runtime.area==values[3] &&
        runtime.reagentType==values[4] && runtime.state==values[5] &&
        runtime.settingFingerprint==source.setting && runtime.bottleFingerprint==source.bottle &&
        runtime.informationFingerprint==source.information;
}

bool knownConfigured(const CurrentHoleRuntime &runtime)
{
    const QStringList types{QStringLiteral(" "),QStringLiteral("r_reagent"),QStringLiteral("s_ibs"),
        QStringLiteral("cleaner"),QStringLiteral("s_control"),QStringLiteral("s_calibrator"),QStringLiteral("s_Factor")};
    const QStringList states{QStringLiteral("r_normal"),QStringLiteral("r_warning"),QStringLiteral("r_useout"),QStringLiteral("r_outdate")};
    return !runtime.reagent.isEmpty() && runtime.reagent!=QStringLiteral("none") && !runtime.bottle.isEmpty() &&
        (runtime.area==QStringLiteral("r_area") || runtime.area==QStringLiteral("s_area")) &&
        types.contains(runtime.reagentType) && states.contains(runtime.state);
}

bool confirmedRuntime(const CurrentHoleRuntime &runtime,const ReagentSnapshot &snapshot,
                      const ReagentHole &hole,const SnapshotSource &source,MaximumSource kind,QString *message)
{
    if ((kind!=MaximumSource::ConfirmedUr && kind!=MaximumSource::ManualHoleSave) || !runtime.available ||
        !runtime.revision || runtime.revision==std::numeric_limits<quint64>::max() || runtime.maximumSource!=kind ||
        !sameRuntimeIdentity(runtime,snapshot,hole,source) || !knownConfigured(runtime) ||
        !std::isfinite(runtime.maximumMicrolitres) || !std::isfinite(runtime.amountMicrolitres) ||
        !std::isfinite(runtime.usedMicrolitres))
        return fail(message,QStringLiteral("当前孔没有对应本次原来源的可用运行值、原身份或三库版本。"));
    double storedUsed=0;
    if (!displayNumber(hole.originalValues.value(QStringLiteral("used_ul")),&storedUsed))
        return fail(message,QStringLiteral("当前数据库used为NULL、BLOB或不可确认的有限数，显示仍待核。"));
    // 只限制解除任务显示待核，不改原Ur setter；空used保旧值可能与任务后的库量不同。
    if (kind==MaximumSource::ConfirmedUr && storedUsed!=runtime.usedMicrolitres)
        return fail(message,QStringLiteral("Ur运行used与当前数据库显示转换不同，尚不足以解除任务显示待核。"));
    // 手工保存已由原setter按瓶型原精度核准，不能再把g6数据库文字强等于独立double。
    return true;
}

bool clearedRow(const ReagentHole &hole)
{
    const auto &row=hole.originalValues;
    QString reagent,type,bottle,batch,state;
    double amount=0,used=0;
    return rawText(row,QStringLiteral("reagent_name"),&reagent) && reagent==QStringLiteral("none") &&
        rawText(row,QStringLiteral("reagent_type"),&type) && type==QStringLiteral(" ") &&
        rawText(row,QStringLiteral("bottle_type"),&bottle) && bottle.isEmpty() &&
        rawText(row,QStringLiteral("reagent_batch"),&batch) && batch.isEmpty() &&
        rawText(row,QStringLiteral("state"),&state) && state==QStringLiteral("r_none") &&
        !row.value(QStringLiteral("amount_uL")).toString().isEmpty() &&
        displayNumber(row.value(QStringLiteral("amount_uL")),&amount) && amount==0 &&
        displayNumber(row.value(QStringLiteral("used_ul")),&used) && used==0;
}

bool urScope(const QByteArray &payload,QStringList *names,bool *full,QString *message)
{
    // 这里只认原完整括号形状、孔名及末标记；数值和实际保存关联仍由原Provider核对。
    if (!payload.startsWith("Ur") || payload.size()>128000)
        return fail(message,QStringLiteral("来源票据不是完整原Ur报文。"));
    QStringList fields;
    for (int cursor=2;cursor<payload.size();)
    {
        if (payload[cursor++]!='[') return fail(message,QStringLiteral("Ur来源字段缺少左括号。"));
        const int end=payload.indexOf(']',cursor);
        if (end<cursor || end-cursor>4096 || fields.size()>=4096)
            return fail(message,QStringLiteral("Ur来源字段不完整或超过原上限。"));
        const QByteArray raw=payload.mid(cursor,end-cursor);
        for (unsigned char ch : raw)
            if (ch<32 || ch=='[') return fail(message,QStringLiteral("Ur来源含控制字符或嵌套括号。"));
        fields.append(QString::fromLatin1(raw)); cursor=end+1;
    }
    bool countOk=false;
    const int count=fields.value(0).toInt(&countOk);
    if (fields.size()<2 || !countOk || count<0 || count>128 ||
        (fields.size()!=1+15*count && fields.size()!=2+15*count))
        return fail(message,QStringLiteral("Ur来源孔数与完整15字段清单不符。"));
    const bool marker=fields.size()==2+15*count;
    if (marker && fields.last()!=QStringLiteral("0") && fields.last()!=QStringLiteral("1"))
        return fail(message,QStringLiteral("Ur来源的全盘或单孔标记未知。"));
    *full=!marker || fields.last()==QStringLiteral("0");
    names->clear(); QSet<QString> unique;
    for (int i=0;i<count;++i)
    {
        const QString name=fields[1+15*i];
        if (name.isEmpty() || unique.contains(name))
            return fail(message,QStringLiteral("Ur来源孔名为空或重复。"));
        unique.insert(name); names->append(name);
    }
    return true;
}

std::shared_ptr<SourceTicket> captureTicket(const Bridge &bridge,const ReagentSnapshot &snapshot,
    const QStringList &active,const OriginalLevels::Context &context,quint64 connection,
    MaximumSource kind,const QStringList &scope,bool full,ReagentsPage *page,QString *message)
{
    if (!bridge.identity.available || connection!=bridge.identity.connection || !context.permissionRevision ||
        !observationMatches(context,bridge.identity) || snapshot.originalRoot!=bridge.identity.root)
    { fail(message,QStringLiteral("来源没有当前原连接、任务身份或N观察许可。")); return {}; }
    SnapshotSource source;
    if (!inspectSnapshot(snapshot,&source,message)) return {};
    const QSet<QString> wanted(scope.cbegin(),scope.cend());
    auto ticket=std::make_shared<SourceTicket>();
    ticket->epoch=bridge.epoch; ticket->connection=connection; ticket->identityRevision=bridge.identity.revision;
    ticket->observationPermission=context.permissionRevision; ticket->root=snapshot.originalRoot;
    ticket->kind=kind; ticket->full=full; ticket->afterProcessedSerial=bridge.processedSerial;
    ticket->deadline=Clock::now()+std::chrono::seconds(10);
    for (const auto &name : active)
    {
        if (!full && !wanted.contains(name)) continue;
        const auto *hole=source.holes.value(name,nullptr);
        if (!hole) { fail(message,QStringLiteral("来源范围没有对应当前活动孔的唯一原行。")); return {}; }
        ticket->rows.insert(name,hole->id);
        ticket->beforeRevision.insert(name,page->originalLevelRuntime(name).revision);
    }
    if (ticket->rows.isEmpty())
    { fail(message,QStringLiteral("来源没有本页面实际活动的孔，未登记恢复票据。")); return {}; }
    return ticket;
}

void clearSourceCache(Bridge *bridge)
{
    bridge->confirmedSources.clear();
    bridge->independentFullIdentity=0; bridge->independentFullSerial=0;
}

void armSourceDeadline(QObject *owner,const std::function<void()> &check)
{
    const QString name=QStringLiteral("sn100TaskReagentSourceDeadline");
    auto *timer=owner->findChild<QTimer *>(name,Qt::FindDirectChildrenOnly);
    if (!timer)
    {
        timer=new QTimer(owner); timer->setObjectName(name);
        timer->setSingleShot(true); timer->setTimerType(Qt::PreciseTimer);
        QObject::connect(timer,&QTimer::timeout,owner,check);
    }
    timer->start(10000); // 只复用一个计时器，不为已结束来源保留一串延迟回调。
}
}

void frmMain::taskReagentContextChanged()
{
    if (!taskReagentBridge_ || taskReagentBridge_->identityChanging) return;
    auto &bridge=*taskReagentBridge_;
    QScopedValueRollback<bool> identityChange(bridge.identityChanging,true);
    TaskIdentity next;
    bool configuredOk=false,reportedOk=false;
    next.configuredModel=property("configuredDeviceModel").toString();
    next.configuredPositions=property("configuredReagentPositions").toInt(&configuredOk);
    next.root=reagents_ ? reagents_->property("originalRoot").toString() : QString();
    const QString sampleRoot=samples_ ? samples_->property("originalRoot").toString() : QString();
    bool connected=false;
    if (deviceControl_)
    {
        const auto &state=deviceControl_->state();
        connected=state.connected;
        next.connection=deviceControl_->connectionGeneration();
        next.actualModel=state.status.model;
        next.protocolToken=state.status.generalFields.value(19);
        next.reportedPositions=state.status.generalFields.value(20).toInt(&reportedOk);
    }
    // 来源临时重新预读时保留上次sourceReady核准的资料，不能因source_暂空撤销任务身份。
    const auto source=bridge.verifiedSource;
    if (source && source->root==next.root)
    {
        next.themeFingerprint=source->themeFingerprint;
        next.capacityRelativeFile=source->capacityRelativeFile;
        next.capacityFingerprint=source->capacityFingerprint;
        next.capacityFileState=capacityState(source->capacityFileState);
        next.capacityCalibrationActive=source->capacityCalibrationActive;
    }
    const QString expectedCapacity=next.actualModel==QStringLiteral("SN450") ?
        QStringLiteral("calibrationFile/ReagentAmount_450.json") : QStringLiteral("calibrationFile/ReagentAmount_600.json");
    const bool capacityOk=source && !next.capacityFileState.isEmpty() && next.capacityRelativeFile==expectedCapacity &&
        ((source->capacityFileState==OriginalRuntime::ReagentCapacityFileState::Present && !next.capacityFingerprint.isEmpty()) ||
         (source->capacityFileState==OriginalRuntime::ReagentCapacityFileState::Missing && next.capacityFingerprint.isEmpty()));
    next.available=connected && next.connection && !next.root.isEmpty() && QDir::isAbsolutePath(next.root) &&
        next.root==sampleRoot && reagents_ && reagents_->snapshot().originalProfile && samples_ && samples_->originalProfile() &&
        !next.actualModel.isEmpty() && next.actualModel==next.configuredModel && !next.protocolToken.isEmpty() &&
        configuredOk && reportedOk && (next.configuredPositions==20 || next.configuredPositions==42) &&
        next.configuredPositions==next.reportedPositions && source && source->root==next.root &&
        source->configuredModel==next.configuredModel && source->configuredPositions==next.configuredPositions &&
        !next.themeFingerprint.isEmpty() && capacityOk && !next.capacityCalibrationActive;
    if (bridge.identityRevision==std::numeric_limits<quint64>::max() && !bridge.identity.revision)
        next.available=false; // 耗尽后的禁用身份保持稳定，不能在每次Qj重复撤销同一身份。
    const bool first=!bridge.identityRevision;
    const bool sessionChanged=first || !sameTaskSession(bridge.identitySession,session_);
    const bool changed=first || !sameIdentity(bridge.identity,next) || sessionChanged;
    if (changed)
    {
        cancelTaskReagentContexts(QStringLiteral("实际连接、原资料、机型协议或账户身份已变化，旧任务依据归回原动作后撤销。"));
        bridge.sourceTicket.reset(); clearSourceCache(&bridge);
        if (bridge.identityRevision==std::numeric_limits<quint64>::max())
        {
            next.available=false; next.revision=0;
            reportTaskReagent(QStringLiteral("任务身份编号已耗尽，未回绕或复用旧身份。"));
        }
        else next.revision=++bridge.identityRevision;
        bridge.identity=next; bridge.identitySession=session_;
    }
    else next=bridge.identity;
    if (reagents_)
    {
        // Page的任务身份同时登记当时账户；仅真实账户变化才同步session，避免每条Qj重复下发。
        if (sessionChanged) reagents_->setSession(session_);
        reagents_->setOriginalTaskReagentIdentity(next);
    }
}

void frmMain::taskReagentSourceReviewed(bool success)
{
    if (!taskReagentBridge_) return;
    if (success && reagentLevelSource_)
        taskReagentBridge_->verifiedSource=std::make_shared<const OriginalLevels::Context>(*reagentLevelSource_);
    else taskReagentBridge_->verifiedSource.reset();
    taskReagentContextChanged();
    checkTaskReagentSource();
    QTimer::singleShot(0,this,[this](){advanceTaskReagents();});
}

void frmMain::beginTaskReagentUrSource(const QByteArray &payload,quint64 connection)
{
    if (!taskReagentBridge_ || !reagents_) return;
    taskReagentContextChanged();
    auto &bridge=*taskReagentBridge_;
    if (bridge.sourceTicket || reagents_->hasUnconfirmedOriginalLevelRuntime())
    {
        bridge.sourceTicket.reset(); clearSourceCache(&bridge);
        reportTaskReagent(QStringLiteral("Ur来源与另一份待核来源重叠，恢复票据已撤销，任务显示仍待核。"));
        return;
    }
    QString error; QStringList scope; bool full=false;
    if (!urScope(payload,&scope,&full,&error)) { reportTaskReagent(error); return; }
    if (!reagentLevelContext_ || !reagentLevelWorker_ || !reagentLevelWorker_->isContextCurrent(*reagentLevelContext_) ||
        !reagentLevelContext_->linuxNew || !sameTaskSession(bridge.identitySession,session_))
    { reportTaskReagent(QStringLiteral("实际Ur没有当前LinuxNew观察上下文，未登记任务显示恢复来源。")); return; }
    bridge.sourceTicket=captureTicket(bridge,reagents_->snapshot(),reagents_->originalTaskReagentActiveHoleNames(),
        *reagentLevelContext_,connection,MaximumSource::ConfirmedUr,scope,full,reagents_,&error);
    if (!bridge.sourceTicket) { reportTaskReagent(error); return; }
    bridge.sourceTicket->payloadSha256=QString::fromLatin1(QCryptographicHash::hash(payload,QCryptographicHash::Sha256).toHex());
    for (const auto &name : bridge.sourceTicket->rows.keys()) bridge.confirmedSources.remove(name);
    bridge.independentFullIdentity=0; bridge.independentFullSerial=0;
    armSourceDeadline(this,[this](){checkTaskReagentSource();});
}

void frmMain::beginTaskReagentManualSource(const QString &hole,quint64 connection)
{
    if (!taskReagentBridge_ || !reagents_) return;
    taskReagentContextChanged();
    auto &bridge=*taskReagentBridge_;
    if (bridge.sourceTicket || reagents_->hasUnconfirmedOriginalLevelRuntime())
    {
        bridge.sourceTicket.reset(); clearSourceCache(&bridge);
        reportTaskReagent(QStringLiteral("手工来源与另一份待核来源重叠，恢复票据已撤销，任务显示仍待核。"));
        return;
    }
    if (!reagentLevelContext_ || !reagentLevelWorker_ || !reagentLevelWorker_->isContextCurrent(*reagentLevelContext_) ||
        !sameTaskSession(bridge.identitySession,session_))
    { reportTaskReagent(QStringLiteral("手工来源没有当前原观察上下文，未登记任务显示恢复来源。")); return; }
    QString error;
    bridge.sourceTicket=captureTicket(bridge,reagents_->snapshot(),reagents_->originalTaskReagentActiveHoleNames(),
        *reagentLevelContext_,connection,MaximumSource::ManualHoleSave,QStringList{hole},false,reagents_,&error);
    if (!bridge.sourceTicket) { reportTaskReagent(error); return; }
    bridge.confirmedSources.remove(hole);
    bridge.independentFullIdentity=0; bridge.independentFullSerial=0;
    armSourceDeadline(this,[this](){checkTaskReagentSource();});
}

void frmMain::finishTaskReagentSourceEnqueue()
{
    if (!taskReagentBridge_ || !taskReagentBridge_->sourceTicket) return;
    auto &bridge=*taskReagentBridge_;
    if (bridge.sourceTicket->queued || !reagents_ || !reagents_->hasUnconfirmedOriginalLevelRuntime())
    {
        bridge.sourceTicket.reset();
        reportTaskReagent(QStringLiteral("原来源未形成唯一一次待核入队，未把confirm调用当作读回成功。"));
        return;
    }
    bridge.sourceTicket->queued=true; // begin已确认原false；仅这次false到true能认领原Page读取。
    QTimer::singleShot(0,this,[this](){checkTaskReagentSource();});
}

void frmMain::checkTaskReagentSource()
{
    if (!taskReagentBridge_ || taskReagentBridge_->identityChanging || !reagents_) return;
    taskReagentContextChanged();
    auto &bridge=*taskReagentBridge_;
    const auto ticket=bridge.sourceTicket;
    if (ticket && Clock::now()>=ticket->deadline)
    {
        bridge.sourceTicket.reset();
        reportTaskReagent(QStringLiteral("原来源恢复票据10秒未收尾，任务显示仍待核；未重发或改原运行值。"));
        return;
    }
    if (reagents_->hasUnconfirmedOriginalLevelRuntime() || (ticket && !ticket->queued)) return;
    if (!bridge.identity.available || !sameTaskSession(bridge.identitySession,session_))
    { bridge.sourceTicket.reset(); clearSourceCache(&bridge); return; }
    const ReagentSnapshot snapshot=reagents_->snapshot();
    const QStringList active=reagents_->originalTaskReagentActiveHoleNames();
    const QSet<QString> activeSet(active.cbegin(),active.cend());
    SnapshotSource source; QString error;
    if (snapshot.originalRoot!=bridge.identity.root || !inspectSnapshot(snapshot,&source,&error))
    {
        bridge.sourceTicket.reset(); clearSourceCache(&bridge);
        if (ticket) reportTaskReagent(error.isEmpty() ? QStringLiteral("来源读回已换原资料根，任务显示仍待核。") : error);
        return;
    }
    bool cacheChanged=false;
    for (auto entry=bridge.confirmedSources.begin();entry!=bridge.confirmedSources.end();)
    {
        const auto *hole=source.holes.value(entry.key(),nullptr);
        const auto runtime=reagents_->originalLevelRuntime(entry.key());
        QString ignored;
        if (!activeSet.contains(entry.key()) || !hole || entry->identityRevision!=bridge.identity.revision ||
            entry->root!=snapshot.originalRoot || entry->rowId!=hole->id || entry->runtimeRevision!=runtime.revision ||
            !confirmedRuntime(runtime,snapshot,*hole,source,entry->kind,&ignored))
        { entry=bridge.confirmedSources.erase(entry); cacheChanged=true; }
        else ++entry;
    }
    if (cacheChanged) { bridge.independentFullIdentity=0; bridge.independentFullSerial=0; }
    bool fullConfirmed=false;
    QSet<QString> cleared;
    QStringList failures;
    if (ticket)
    {
        const bool contextCurrent=reagentLevelContext_ && reagentLevelWorker_ &&
            reagentLevelContext_->permissionRevision==ticket->observationPermission &&
            observationMatches(*reagentLevelContext_,bridge.identity) &&
            reagentLevelWorker_->isContextCurrent(*reagentLevelContext_);
        if (!ticket->queued || ticket->epoch!=bridge.epoch || ticket->connection!=bridge.identity.connection ||
            ticket->identityRevision!=bridge.identity.revision || ticket->root!=snapshot.originalRoot || !contextCurrent)
        {
            bridge.sourceTicket.reset();
            reportTaskReagent(QStringLiteral("来源等待期间原连接、观察许可、任务身份或资料已变化，未认证到新上下文。"));
            return;
        }
        int trusted=0;
        bool allCovered=ticket->full && ticket->kind==MaximumSource::ConfirmedUr && !active.isEmpty();
        for (const auto &name : active)
        {
            const auto *hole=source.holes.value(name,nullptr);
            if (!ticket->rows.contains(name)) { allCovered=false; continue; }
            const auto runtime=reagents_->originalLevelRuntime(name);
            const bool advanced=hole && ticket->rows.value(name)==hole->id && ticket->beforeRevision.contains(name) &&
                runtime.revision>ticket->beforeRevision.value(name) && runtime.revision!=std::numeric_limits<quint64>::max();
            QString reason;
            if (advanced && confirmedRuntime(runtime,snapshot,*hole,source,ticket->kind,&reason))
            {
                SourceEvidence evidence;
                evidence.identityRevision=bridge.identity.revision; evidence.runtimeRevision=runtime.revision;
                evidence.afterProcessedSerial=ticket->afterProcessedSerial; evidence.rowId=hole->id;
                evidence.root=snapshot.originalRoot; evidence.kind=ticket->kind;
                bridge.confirmedSources.insert(name,evidence); ++trusted;
            }
            else if (advanced && ticket->full && ticket->kind==MaximumSource::ConfirmedUr && clearedRow(*hole) &&
                !runtime.available && runtime.maximumSource==MaximumSource::Unknown && sameRuntimeIdentity(runtime,snapshot,*hole,source))
                cleared.insert(name); // 仅作本次全盘覆盖的清孔证据，运行来源继续Unknown。
            else
            {
                allCovered=false;
                failures.append(name+QStringLiteral(":")+(reason.isEmpty() ?
                    QStringLiteral("孔原id或运行revision未证明本次来源已采用") : reason));
            }
        }
        fullConfirmed=allCovered && trusted>0;
        if (!fullConfirmed) cleared.clear(); // 单靠清盘或未知孔不能宣称整盘来源恢复。
        if (fullConfirmed)
        {
            bridge.independentFullIdentity=bridge.identity.revision;
            bridge.independentFullSerial=ticket->afterProcessedSerial;
        }
        bridge.sourceTicket.reset();
    }
    bool recovered=false;
    auto latch=bridge.displayUnknown.find(snapshot.originalRoot);
    if (latch!=bridge.displayUnknown.end())
    {
        for (auto pending=latch->holes.begin();pending!=latch->holes.end();)
        {
            const auto evidence=bridge.confirmedSources.constFind(pending.key());
            const bool known=evidence!=bridge.confirmedSources.cend() && evidence->rowId==pending.value() &&
                evidence->afterProcessedSerial>=latch->afterProcessedSerial;
            const bool clearedByFull=fullConfirmed && ticket->afterProcessedSerial>=latch->afterProcessedSerial &&
                cleared.contains(pending.key()) && ticket->rows.value(pending.key())==pending.value();
            if (known || clearedByFull) { pending=latch->holes.erase(pending); recovered=true; }
            else ++pending;
        }
        if (latch->unscoped && fullConfirmed && ticket->afterProcessedSerial>=latch->afterProcessedSerial)
        { latch->unscoped=false; recovered=true; }
        if (!latch->unscoped && latch->holes.isEmpty()) bridge.displayUnknown.erase(latch);
    }
    // 全局聚合仍保留其他旧根的未知；本次full只给当前身份留下独立证据，绝不清事务未知。
    if (ticket)
    {
        if (!failures.isEmpty())
            reportTaskReagent(QStringLiteral("本次原来源只恢复已核范围；仍待核%1孔：%2")
                .arg(failures.size()).arg(failures.mid(0,4).join(QStringLiteral("；"))));
        else if (recovered || fullConfirmed)
            reportTaskReagent(QStringLiteral("原来源已按本次孔位、运行版本和保存行核对；仅解除同根已证范围的任务显示待核，其他未知保留。"));
    }
    if (recovered || fullConfirmed || cacheChanged)
        QTimer::singleShot(0,this,[this](){advanceTaskReagents();});
}
