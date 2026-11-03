#include "original_reagent_level_runtime.h"
#include <QSet>
#include <cmath>
#include <limits>

namespace OriginalLevels
{
namespace
{
struct Versions
{
    QString setting, bottle, information;
};

bool fail(QString *message, const QString &reason)
{
    if (message) *message=reason;
    return false;
}

bool finiteNumber(const QVariant &value, double *number, bool emptyIsZero=false)
{
    const QString text=value.toString();
    if (emptyIsZero && text.isEmpty()) { *number=0; return true; }
    bool ok=false;
    *number=text.toDouble(&ok);
    return ok && std::isfinite(*number);
}

bool integerText(const QString &text, int *number)
{
    bool ok=false;
    *number=text.toInt(&ok);
    return ok;
}

bool sameValue(const QVariant &first, const QVariant &second)
{
    return first.userType()==second.userType() && first.isNull()==second.isNull() && first==second;
}

bool sameRow(const QVariantMap &first, const QVariantMap &second, const QSet<QString> &ignored={})
{
    if (first.keys()!=second.keys()) return false;
    for (auto it=first.cbegin();it!=first.cend();++it)
        if (!ignored.contains(it.key()) && !sameValue(it.value(),second.value(it.key()))) return false;
    return true;
}

bool collect(const QString &fingerprint, QString *version)
{
    if (fingerprint.isEmpty() || (!version->isEmpty() && *version!=fingerprint)) return false;
    *version=fingerprint;
    return true;
}

const ReagentHole *findHole(const ReagentSnapshot &snapshot, const QString &name)
{
    for (const auto &hole : snapshot.holes) if (hole.name==name) return &hole;
    return nullptr;
}

const QStringList &states()
{
    static const QStringList values{"r_none","r_normal","r_warning","r_useout","r_unknown","r_outdate"};
    return values;
}

bool inspect(const ReagentSnapshot &snapshot, Versions *versions, QString *message)
{
    *versions=Versions();
    if (!snapshot.originalProfile || snapshot.originalRoot.isEmpty())
        return fail(message,QStringLiteral("不是已读取的原资料快照，运行值不可用。"));
    QSet<QString> names;
    const QStringList required{"id","hole_name","area","reagent_type","reagent_name","reagent_batch",
        "bottle_type","amount_uL","used_ul","state","priority","datetime","out_date","formula_level",
        "out_ul","warning_ul","max_ul"};
    for (const auto &hole : snapshot.holes)
    {
        bool idOk=false;
        const auto &row=hole.originalValues;
        const qint64 id=row.value("id").toLongLong(&idOk);
        if (!idOk || id<=0 || id!=hole.id || hole.name.isEmpty() || hole.name!=row.value("hole_name").toString() ||
            names.contains(hole.name) || !collect(hole.originalFingerprint,&versions->setting))
            return fail(message,QStringLiteral("原孔位身份、唯一性或孔库指纹不完整。"));
        for (const auto &key : required)
            if (!row.contains(key)) return fail(message,QStringLiteral("孔位快照缺少原字段：")+key);
        names.insert(hole.name);
    }
    for (const auto &bottle : snapshot.bottles)
        if (bottle.originalValues.isEmpty() || !collect(bottle.originalFingerprint,&versions->bottle))
            return fail(message,QStringLiteral("原瓶型行或瓶型库指纹不完整。"));
    for (const auto &reagent : snapshot.reagents)
        if (reagent.originalValues.isEmpty() || !collect(reagent.originalFingerprint,&versions->information))
            return fail(message,QStringLiteral("原试剂行或试剂库指纹不完整。"));
    for (const auto &batch : snapshot.batches)
        if (!collect(batch.originalFingerprint,&versions->information))
            return fail(message,QStringLiteral("原批组与试剂库指纹不一致。"));
    for (const auto &barcode : snapshot.barcodes)
        if (!collect(barcode.originalFingerprint,&versions->information))
            return fail(message,QStringLiteral("原条码与试剂库指纹不一致。"));
    if (versions->setting.isEmpty() || versions->bottle.isEmpty() || versions->information.isEmpty())
        return fail(message,QStringLiteral("快照没有足够原始行证明试剂、瓶型及孔位三库版本。"));
    return true;
}

void bindVersions(CurrentHoleRuntime *runtime, const Versions &versions)
{
    runtime->settingFingerprint=versions.setting;
    runtime->bottleFingerprint=versions.bottle;
    runtime->informationFingerprint=versions.information;
}

CurrentHoleRuntime identity(const ReagentSnapshot &snapshot, const ReagentHole &hole, const Versions &versions)
{
    CurrentHoleRuntime runtime;
    const auto &row=hole.originalValues;
    runtime.root=snapshot.originalRoot; runtime.hole=hole.name;
    runtime.reagent=row.value("reagent_name").toString(); runtime.bottle=row.value("bottle_type").toString();
    runtime.area=row.value("area").toString(); runtime.reagentType=row.value("reagent_type").toString();
    runtime.state=row.value("state").toString();
    bindVersions(&runtime,versions);
    return runtime;
}

bool configuredIdentity(const CurrentHoleRuntime &runtime)
{
    static const QStringList types{" ","r_reagent","s_ibs","cleaner","s_control","s_calibrator","s_Factor"};
    return !runtime.hole.isEmpty() && !runtime.reagent.isEmpty() && runtime.reagent!=QStringLiteral("none") &&
        !runtime.bottle.isEmpty() && (runtime.area==QStringLiteral("r_area") || runtime.area==QStringLiteral("s_area")) &&
        types.contains(runtime.reagentType) && states().contains(runtime.state) &&
        runtime.state!=QStringLiteral("r_none") && runtime.state!=QStringLiteral("r_unknown");
}

bool sameIdentity(const CurrentHoleRuntime &first, const CurrentHoleRuntime &second)
{
    return first.root==second.root && first.hole==second.hole && first.reagent==second.reagent &&
        first.bottle==second.bottle && first.area==second.area && first.reagentType==second.reagentType;
}

QList<QVariantMap> bottleEvidence(const ReagentSnapshot &snapshot, const QString &name)
{
    QList<QVariantMap> rows;
    for (const auto &bottle : snapshot.bottles)
        if (bottle.originalValues.value("bottle_type_name").toString()==name) rows.append(bottle.originalValues);
    return rows;
}

bool sameBottles(const ReagentSnapshot &first, const ReagentSnapshot &second, const QString &name)
{
    const auto a=bottleEvidence(first,name), b=bottleEvidence(second,name);
    if (a.size()!=b.size()) return false;
    for (int i=0;i<a.size();++i) if (!sameRow(a[i],b[i])) return false;
    return true;
}

bool bottleMaximum(const ReagentSnapshot &snapshot, const QString &name, const QString &area,
                   bool useArea, double *maximum, QString *message)
{
    // 原表id是INTEGER PRIMARY KEY AUTOINCREMENT，但getBottleTypeHash是无ORDER BY的SELECT *。
    // 不把Linux的ORDER BY id当任意资料的原序保证；同名最大量一致时才无上限选择歧义。
    bool found=false;
    for (const auto &bottle : snapshot.bottles)
    {
        const auto &row=bottle.originalValues;
        if (row.value("bottle_type_name").toString()!=name || (useArea && row.value("area").toString()!=area)) continue;
        double amount=0;
        if (!finiteNumber(row.value("max_amount_ul"),&amount) || amount<0 || amount>std::numeric_limits<int>::max())
            return fail(message,QStringLiteral("原瓶型最大量不是可确认的有限非负值。"));
        if (found && amount!=*maximum)
            return fail(message,useArea ? QStringLiteral("同区域同名瓶型最大量不同，手工保存来源尚未确认。") :
                QStringLiteral("同名瓶型最大量不同，原SELECT无排序，不能按Linux行序或区域猜初始化上限；待已核Ur。"));
        if (!found) *maximum=amount;
        found=true;
    }
    return found || fail(message,QStringLiteral("没有可确认的原瓶型最大量，不采用控件默认5000。"));
}

const ReagentRecord *definition(const ReagentSnapshot &snapshot, const QString &name)
{
    const ReagentRecord *found=nullptr;
    for (const auto &record : snapshot.reagents)
        if (record.originalValues.value("reagent_name").toString()==name)
        {
            if (found) return nullptr;
            found=&record;
        }
    return found;
}

QString initialRuntime(const ReagentSnapshot &snapshot, const ReagentHole &hole, CurrentHoleRuntime *runtime)
{
    if (!configuredIdentity(*runtime)) return QStringLiteral("清孔、未知孔或孔位配置身份不完整，没有可信运行来源。");
    QString reason;
    if (!bottleMaximum(snapshot,runtime->bottle,runtime->area,false,&runtime->maximumMicrolitres,&reason)) return reason;
    if (!finiteNumber(hole.originalValues.value("amount_uL"),&runtime->amountMicrolitres) ||
        !finiteNumber(hole.originalValues.value("used_ul"),&runtime->usedMicrolitres,true))
        return QStringLiteral("原amount或used不是有限数，不能建立初始化运行快照。");
    runtime->amountMicrolitres=qMin(runtime->amountMicrolitres,runtime->maximumMicrolitres);
    const auto *reagent=definition(snapshot,runtime->reagent);
    if (!reagent) return QStringLiteral("没有唯一原试剂定义，初始化阈值状态不可确认。");
    double out=0, warning=0;
    if (!finiteNumber(reagent->originalValues.value("out_ul"),&out) ||
        !finiteNumber(reagent->originalValues.value("warning_ul"),&warning))
        return QStringLiteral("初始化试剂阈值不是有限数。");
    if (runtime->amountMicrolitres<=out) runtime->state=QStringLiteral("r_useout");
    else if (runtime->amountMicrolitres<=warning) runtime->state=QStringLiteral("r_warning");
    runtime->available=true; runtime->maximumSource=MaximumSource::InitialBottleByName;
    return hole.originalValues.value("used_ul").toString().isEmpty()
        ? QStringLiteral("原used_ul为空，按原initPanel转换为0；不是amount回退，也不是设备测量。")
        : QStringLiteral("按原初始化的同名瓶型上限建立运行值，未使用孔库max_ul。");
}

bool decodeUr(const QByteArray &payload, QList<QStringList> *rows, bool *single, QString *message)
{
    if (!payload.startsWith("Ur") || payload.size()>128000)
        return fail(message,QStringLiteral("不是完整的原Ur资料回读。"));
    QStringList fields;
    for (int cursor=2;cursor<payload.size();)
    {
        if (payload[cursor++]!='[') return fail(message,QStringLiteral("Ur字段缺少左括号。"));
        const int end=payload.indexOf(']',cursor);
        if (end<cursor || end-cursor>4096 || fields.size()>=4096)
            return fail(message,QStringLiteral("Ur字段不完整或超过上限。"));
        const QByteArray raw=payload.mid(cursor,end-cursor);
        for (unsigned char ch : raw)
            if (ch<32 || ch=='[') return fail(message,QStringLiteral("Ur字段含控制字符或嵌套括号。"));
        fields.append(QString::fromLatin1(raw)); cursor=end+1;
    }
    int count=0;
    if (fields.size()<2 || !integerText(fields.first(),&count) || count<0 || count>128 ||
        (fields.size()!=1+15*count && fields.size()!=2+15*count))
        return fail(message,QStringLiteral("Ur孔数与完整15字段清单不对应。"));
    const bool hasMarker=fields.size()==2+15*count;
    if (hasMarker && fields.last()!=QStringLiteral("0") && fields.last()!=QStringLiteral("1"))
        return fail(message,QStringLiteral("Ur完整/单孔标记未知。"));
    *single=hasMarker && fields.last()==QStringLiteral("1");
    rows->clear(); QSet<QString> names;
    for (int i=0;i<count;++i)
    {
        const QStringList row=fields.mid(1+15*i,15);
        int state=0, maximum=0, out=0, warning=0, priority=0;
        double amount=0, used=0;
        if (row[0].isEmpty() || names.contains(row[0]) || !integerText(row[9],&state) || state<0 || state>=states().size() ||
            !integerText(row[13],&maximum) || maximum<0 || !integerText(row[6],&out) || out<0 ||
            !integerText(row[12],&warning) || warning<0 || !integerText(row[8],&priority) || priority<0 ||
            !finiteNumber(row[7],&amount) || amount<0 || (!row[14].isEmpty() && (!finiteNumber(row[14],&used) || used<0)))
            return fail(message,QStringLiteral("Ur孔位重复或整数/液量字段无效，未沿原弱转换默认为0。"));
        names.insert(row[0]); rows->append(row);
    }
    return true;
}

bool matchesStoredUr(const QStringList &wire, const QVariantMap &stored, QString *message)
{
    static const QStringList columns{"hole_name","reagent_type","reagent_name","reagent_batch","bottle_type",
        "formula_level","out_ul","amount_uL","priority","state","datetime","out_date","warning_ul","max_ul","used_ul"};
    const QSet<int> numeric{6,7,8,12,13};
    for (int i=0;i<columns.size();++i)
    {
        if (i==14 && wire[i].isEmpty()) continue; // 当前Repo此项不写，原setter也保留旧运行used。
        if (numeric.contains(i))
        {
            double first=0,second=0;
            if (!finiteNumber(wire[i],&first) || !finiteNumber(stored.value(columns[i]),&second) || first!=second)
                return fail(message,QStringLiteral("Ur数值与已保存原行不对应：")+columns[i]);
        }
        else
        {
            const QString expected=i==9 ? states()[wire[i].toInt()] : wire[i];
            if (!stored.contains(columns[i]) || stored.value(columns[i]).toString()!=expected)
                return fail(message,QStringLiteral("Ur字段与已保存原行不对应：")+columns[i]);
        }
    }
    return true;
}

bool knownSource(MaximumSource source)
{
    return source==MaximumSource::InitialBottleByName || source==MaximumSource::ConfirmedUr ||
        source==MaximumSource::ManualHoleSave || source==MaximumSource::FilledHole || source==MaximumSource::ClearedHole;
}
}

void RuntimeProvider::put(CurrentHoleRuntime runtime, const QString &reason)
{
    if (revision_==std::numeric_limits<quint64>::max())
    {
        runtime.available=false; runtime.maximumSource=MaximumSource::Unknown;
        runtime.revision=revision_; diagnostics_[runtime.hole]=QStringLiteral("运行revision已耗尽，必须更换提供器，未回绕旧版本。");
    }
    else { runtime.revision=++revision_; diagnostics_[runtime.hole]=reason; }
    runtimes_[runtime.hole]=runtime;
}

CurrentHoleRuntime RuntimeProvider::current(const QString &hole) const
{
    return runtimes_.value(hole);
}

QString RuntimeProvider::diagnostic(const QString &hole) const
{
    return diagnostics_.value(hole,QStringLiteral("当前孔没有已确认的运行快照。"));
}

void RuntimeProvider::invalidate(const QString &hole)
{
    const QStringList names=hole.isEmpty() ? runtimes_.keys() : QStringList{hole};
    for (const auto &name : names)
    {
        CurrentHoleRuntime runtime=runtimes_.value(name); runtime.hole=name;
        runtime.available=false; runtime.maximumSource=MaximumSource::Unknown;
        put(runtime,QStringLiteral("运行来源或外层身份/连接许可已失效，普通重读不自动恢复。"));
        previousRuntimes_.remove(name); previousTransitionRevisions_.remove(name);
    }
    if (hole.isEmpty()) previousValid_=false;
}

void RuntimeProvider::observeSnapshot(const ReagentSnapshot &snapshot)
{
    Versions versions; QString reason;
    const bool valid=inspect(snapshot,&versions,&reason);
    const bool initial=!observed_ || snapshot.originalRoot!=snapshot_.originalRoot;
    if (!valid)
    {
        invalidate(); snapshot_=snapshot; snapshotValid_=false; previousValid_=false;
        for (const auto &hole : snapshot.holes)
        {
            CurrentHoleRuntime runtime=identity(snapshot,hole,versions);
            put(runtime,reason);
        }
        return;
    }
    if (initial)
    {
        runtimes_.clear(); diagnostics_.clear(); previousRuntimes_.clear(); previousTransitionRevisions_.clear(); previousValid_=false;
        for (const auto &hole : snapshot.holes)
        {
            CurrentHoleRuntime runtime=identity(snapshot,hole,versions);
            const QString explanation=initialRuntime(snapshot,hole,&runtime);
            put(runtime,explanation);
        }
    }
    else
    {
        Versions before; QString ignored;
        const bool changed=!snapshotValid_ || !inspect(snapshot_,&before,&ignored) || before.setting!=versions.setting ||
            before.bottle!=versions.bottle || before.information!=versions.information;
        if (changed)
        {
            previousSnapshot_=snapshot_; previousRuntimes_=runtimes_;
            previousValid_=snapshotValid_; previousTransitionRevisions_.clear();
        }
        QSet<QString> present;
        for (const auto &hole : snapshot.holes)
        {
            present.insert(hole.name);
            const auto *old=findHole(snapshot_,hole.name);
            CurrentHoleRuntime runtime=runtimes_.value(hole.name);
            const auto readIdentity=identity(snapshot,hole,versions);
            if (!old || !sameRow(old->originalValues,hole.originalValues) || !sameIdentity(runtime,readIdentity) ||
                !sameBottles(snapshot_,snapshot,runtime.bottle))
            {
                runtime=readIdentity;
                put(runtime,QStringLiteral("孔原始行、身份或本瓶型资料变化，未取得确认来源，运行值已失效。"));
            }
            else if (runtime.settingFingerprint!=versions.setting || runtime.bottleFingerprint!=versions.bottle ||
                     runtime.informationFingerprint!=versions.information)
            {
                bindVersions(&runtime,versions);
                put(runtime,diagnostic(hole.name));
            }
            if (changed) previousTransitionRevisions_[hole.name]=runtimes_.value(hole.name).revision;
        }
        for (const auto &name : runtimes_.keys()) if (!present.contains(name))
        {
            invalidate(name);
            diagnostics_[name]=QStringLiteral("孔位不再属于当前快照，运行值不可用。");
        }
    }
    snapshot_=snapshot; snapshotValid_=true; observed_=true;
}

bool RuntimeProvider::confirmUr(const QByteArray &payload, bool linuxNew, const ReagentSnapshot &snapshot, QString *message)
{
    if (message) message->clear();
    observeSnapshot(snapshot);
    const auto reject=[&](const QString &reason) { invalidate(); return fail(message,reason); };
    if (!linuxNew) return reject(QStringLiteral("旧协议Ur运行来源尚未核实，本片不猜其setter或保存映射。"));
    Versions versions; QString error;
    if (!snapshotValid_ || !inspect(snapshot,&versions,&error)) return reject(error.isEmpty() ? QStringLiteral("Ur快照无效。") : error);
    QList<QStringList> incoming; bool single=false;
    if (!decodeUr(payload,&incoming,&single,&error)) return reject(error);
    QMap<QString,CurrentHoleRuntime> proposed;
    QSet<QString> received;
    for (const auto &wire : incoming)
    {
        const auto *hole=findHole(snapshot,wire[0]);
        if (!hole || !matchesStoredUr(wire,hole->originalValues,&error))
            return reject(error.isEmpty() ? QStringLiteral("Ur孔不在已保存快照中。") : error);
        received.insert(wire[0]);
        CurrentHoleRuntime runtime=identity(snapshot,*hole,versions);
        if (!configuredIdentity(runtime))
        {
            // 完整Ur常含空孔：该孔失效，不使其他已确认孔永远无法建立运行来源。
            proposed.insert(runtime.hole,runtime);
            continue;
        }
        runtime.state=states()[wire[9].toInt()]; runtime.maximumMicrolitres=wire[13].toInt();
        runtime.amountMicrolitres=wire[7].toDouble();
        if (!wire[14].isEmpty()) runtime.usedMicrolitres=wire[14].toDouble();
        else
        {
            CurrentHoleRuntime previous=runtimes_.value(runtime.hole);
            if ((!previous.available || !sameIdentity(previous,runtime)) && previousValid_)
                previous=previousRuntimes_.value(runtime.hole);
            if (!previous.available || previous.root!=runtime.root || previous.hole!=runtime.hole ||
                !std::isfinite(previous.usedMicrolitres))
                return reject(QStringLiteral("Ur used为空，原setter保留旧运行used，但本孔旧值无可信来源。"));
            if (single)
            {
                const auto *old=findHole(previousSnapshot_,runtime.hole);
                const auto *currentBefore=findHole(snapshot_,runtime.hole);
                const auto *basis=previousValid_ && old ? old : currentBefore;
                if (!basis || basis->originalValues.value("used_ul").toString()!=hole->originalValues.value("used_ul").toString())
                    return reject(QStringLiteral("单孔Ur空used与保存时保留的原used文本不符。"));
            }
            else if (hole->originalValues.value("used_ul").toString()!=QStringLiteral("0"))
                return reject(QStringLiteral("整盘Ur空used与当前Repo清盘保存的0不符。"));
            runtime.usedMicrolitres=previous.usedMicrolitres;
        }
        runtime.available=true; runtime.maximumSource=MaximumSource::ConfirmedUr; proposed.insert(runtime.hole,runtime);
    }
    if (!single)
        for (const auto &hole : snapshot.holes) if (!received.contains(hole.name))
        {
            const auto &row=hole.originalValues;
            double amount=0;
            if (row.value("reagent_name").toString()!=QStringLiteral("none") || row.value("reagent_type").toString()!=QStringLiteral(" ") ||
                !row.value("reagent_batch").toString().isEmpty() || !row.value("bottle_type").toString().isEmpty() ||
                row.value("state").toString()!=QStringLiteral("r_none") || row.value("used_ul").toString()!=QStringLiteral("0") ||
                !finiteNumber(row.value("amount_uL"),&amount) || amount!=0)
                return reject(QStringLiteral("整盘Ur未列出的孔不符合当前Repo实际清盘映射。"));
        }
    if (!single) invalidate();
    for (auto it=proposed.cbegin();it!=proposed.cend();++it)
        put(it.value(),it.value().available ?
            QStringLiteral("Ur完整字段已与当前Repo保存行核对；采用原运行setter，空used时保留已知旧运行值。Ur库formula/amount等与Windows信号回写已有差异，本片未修改；此来源不表示设备动作完成。") :
            QStringLiteral("Ur已对应当前保存行，但该孔已清空、未知或无完整身份，未建立可用N运行来源。"));
    previousValid_=false; previousRuntimes_.clear(); previousTransitionRevisions_.clear();
    if (message) *message=QStringLiteral("Ur保存关联已核，已配置孔的运行来源已更新；清孔/未知孔仍不可用，连接/许可及动作完成由外层判断。");
    return true;
}

bool RuntimeProvider::confirmManualHole(const QString &name, const ReagentSnapshot &snapshot, QString *message)
{
    if (message) message->clear();
    observeSnapshot(snapshot);
    const auto reject=[&](const QString &reason) { invalidate(name); return fail(message,reason); };
    Versions versions; QString error;
    if (!snapshotValid_ || !inspect(snapshot,&versions,&error)) return reject(error.isEmpty() ? QStringLiteral("手工保存快照无效。") : error);
    const auto *hole=findHole(snapshot,name);
    if (!hole) return reject(QStringLiteral("手工保存孔不在当前快照中。"));
    CurrentHoleRuntime runtime=identity(snapshot,*hole,versions);
    if (!configuredIdentity(runtime) || runtime.state!=QStringLiteral("r_normal"))
        return reject(QStringLiteral("手工保存结果为清孔/未知孔或非normal，运行来源仍不可用。"));
    double maximum=0;
    if (!bottleMaximum(snapshot,runtime.bottle,runtime.area,true,&maximum,&error)) return reject(error);
    const auto *reagent=definition(snapshot,runtime.reagent);
    if (!reagent || reagent->originalValues.value("reagent_type").toString()!=runtime.reagentType)
        return reject(QStringLiteral("手工孔类型与唯一原试剂定义不对应。"));
    const auto &row=hole->originalValues;
    double amount=0, maxStored=0, out=0, warning=0, expectedOut=0, expectedWarning=0;
    double formattedMaximum=0;
    if (!finiteNumber(QString::number(maximum),&formattedMaximum) || formattedMaximum>std::numeric_limits<int>::max() ||
        !finiteNumber(row.value("amount_uL"),&amount) || amount!=static_cast<int>(formattedMaximum) ||
        !finiteNumber(row.value("max_ul"),&maxStored) || maxStored!=maximum ||
        row.value("used_ul").toString()!=QString::number(maximum) || row.value("datetime").toString().isEmpty() ||
        !finiteNumber(row.value("out_ul"),&out) || !finiteNumber(row.value("warning_ul"),&warning) ||
        !finiteNumber(reagent->originalValues.value("out_ul"),&expectedOut) ||
        !finiteNumber(reagent->originalValues.value("warning_ul"),&expectedWarning) || out!=expectedOut || warning!=expectedWarning)
        return reject(QStringLiteral("已保存孔字段不符合原手工设置的满瓶量、独立max和试剂阈值，未猜运行来源。"));
    runtime.available=true; runtime.maximumSource=MaximumSource::ManualHoleSave;
    runtime.maximumMicrolitres=maximum; runtime.amountMicrolitres=maximum; runtime.usedMicrolitres=maximum;
    put(runtime,QStringLiteral("本地手工保存字段与本孔区域瓶型及试剂资料对应；不是设备已执行或实测液面。"));
    previousRuntimes_.remove(name); previousTransitionRevisions_.remove(name);
    return true;
}

bool RuntimeProvider::acceptObservation(const QString &beforeSettingFingerprint, const CurrentHoleRuntime &after,
                                      const ReagentSnapshot &persistedSnapshot, QString *message)
{
    if (message) message->clear();
    const auto consumed=acceptedObservationRevisions_.constFind(after.hole);
    if (consumed!=acceptedObservationRevisions_.cend() && after.revision<=consumed.value())
        return fail(message,QStringLiteral("本孔N运行输入revision已消费或更旧，未重复采用旧前版；当前运行值保持。"));
    const auto reject=[&](const QString &reason) { invalidate(after.hole); return fail(message,reason); };
    Versions saved; QString error;
    if (!snapshotValid_ || !inspect(persistedSnapshot,&saved,&error) || beforeSettingFingerprint.isEmpty())
        return reject(error.isEmpty() ? QStringLiteral("N提交后的快照或前版指纹不完整。") : error);
    Versions currentVersions;
    if (!inspect(snapshot_,&currentVersions,&error)) return reject(error);
    const ReagentSnapshot *basis=&snapshot_;
    const QMap<QString,CurrentHoleRuntime> *basisRuntimes=&runtimes_;
    if (currentVersions.setting!=beforeSettingFingerprint)
    {
        Versions previous;
        if (currentVersions.setting!=saved.setting || !previousValid_ || !inspect(previousSnapshot_,&previous,&error) ||
            previous.setting!=beforeSettingFingerprint ||
            previousTransitionRevisions_.value(after.hole)!=runtimes_.value(after.hole).revision)
            return reject(QStringLiteral("当前运行值既不是N前版，也不是其刚保存的版本，未恢复旧观察。"));
        basis=&previousSnapshot_; basisRuntimes=&previousRuntimes_;
    }
    Versions before;
    if (!inspect(*basis,&before,&error)) return reject(error);
    const CurrentHoleRuntime prior=basisRuntimes->value(after.hole);
    const auto *beforeHole=findHole(*basis,after.hole), *savedHole=findHole(persistedSnapshot,after.hole);
    if (!prior.available || !after.available || !prior.revision ||
        prior.revision==std::numeric_limits<quint64>::max() || after.revision!=prior.revision+1 ||
        !knownSource(after.maximumSource) || after.maximumSource!=prior.maximumSource || !sameIdentity(prior,after) ||
        after.root!=persistedSnapshot.originalRoot || after.root!=basis->originalRoot ||
        prior.settingFingerprint!=before.setting || prior.bottleFingerprint!=before.bottle || prior.informationFingerprint!=before.information ||
        before.information!=saved.information || before.bottle!=saved.bottle ||
        after.informationFingerprint!=saved.information || after.bottleFingerprint!=saved.bottle ||
        after.settingFingerprint.isEmpty() || after.settingFingerprint!=saved.setting ||
        after.maximumMicrolitres!=prior.maximumMicrolitres || !std::isfinite(after.amountMicrolitres) ||
        !std::isfinite(after.usedMicrolitres) || after.amountMicrolitres<0 || after.amountMicrolitres>prior.maximumMicrolitres ||
        !states().contains(after.state) || !beforeHole || !savedHole || basis->holes.size()!=persistedSnapshot.holes.size())
        return reject(QStringLiteral("N运行结果的来源、身份、revision或三库版本与前版/已保存行不对应。"));
    const CurrentHoleRuntime savedIdentity=identity(persistedSnapshot,*savedHole,saved);
    if (!sameIdentity(after,savedIdentity)) return reject(QStringLiteral("N保存后的孔身份发生变化。"));
    const QSet<QString> nColumns{QStringLiteral("formula_level"),QStringLiteral("amount_uL"),QStringLiteral("used_ul")};
    for (const auto &old : basis->holes)
    {
        const auto *persisted=findHole(persistedSnapshot,old.name);
        if (!persisted || !sameRow(old.originalValues,persisted->originalValues,old.name==after.hole ? nColumns : QSet<QString>()))
            return reject(QStringLiteral("N提交还改变了其他孔或不属于本N的字段，运行覆盖未应用。"));
    }
    if (!sameBottles(*basis,persistedSnapshot,after.bottle))
        return reject(QStringLiteral("N提交期间本瓶型原行改变。"));
    double formula=0, amount=0, storedUsed=0;
    if (!finiteNumber(savedHole->originalValues.value("formula_level"),&formula) || formula<0 || formula>=0xff00 ||
        !finiteNumber(savedHole->originalValues.value("amount_uL"),&amount) || amount!=std::trunc(amount) ||
        amount<std::numeric_limits<int>::min() || amount>std::numeric_limits<int>::max() ||
        !finiteNumber(savedHole->originalValues.value("used_ul"),&storedUsed,true) ||
        (after.usedMicrolitres!=after.amountMicrolitres &&
         (after.usedMicrolitres!=prior.usedMicrolitres || prior.usedMicrolitres>after.amountMicrolitres)))
        return reject(QStringLiteral("N已保存列或UI used变化不符合可确认的观察结果。"));
    const auto *reagent=definition(persistedSnapshot,after.reagent);
    double out=0, warning=0;
    if (!reagent || !finiteNumber(reagent->originalValues.value("out_ul"),&out) ||
        !finiteNumber(reagent->originalValues.value("warning_ul"),&warning) ||
        after.state!=(after.amountMicrolitres<=out ? QStringLiteral("r_useout") :
            after.amountMicrolitres<=warning ? QStringLiteral("r_warning") : QStringLiteral("r_normal")))
        return reject(QStringLiteral("N界面状态与已保存版本的试剂阈值不对应。"));
    // 调用者已经确认writer提交；这里只接运行覆盖，不能由after重构Testing的数据库原计算量。
    observeSnapshot(persistedSnapshot);
    CurrentHoleRuntime adopted=after; bindVersions(&adopted,saved);
    const quint64 floor=qMax(revision_,runtimes_.value(after.hole).revision);
    if (floor==std::numeric_limits<quint64>::max())
        return reject(QStringLiteral("N已保存，但运行revision已耗尽，未回绕或降低版本采用展示。"));
    // 重读可能已经推进其他孔及全局时钟；采用版本不能被通用put改小，也不能低于输入after。
    adopted.revision=qMax(after.revision,floor+1);
    revision_=adopted.revision;
    runtimes_[adopted.hole]=adopted;
    diagnostics_[adopted.hole]=QStringLiteral("N已提交版本与前版字段范围核对，保留独立UI量/used/state；不把该覆盖当设备完成。")+
        (savedHole->originalValues.value("used_ul").toString().isEmpty()
            ? QStringLiteral(" 原used_ul文本仍为空，运行used沿已知来源保留，不按amount回退。") : QString());
    acceptedObservationRevisions_[after.hole]=after.revision;
    previousValid_=false; previousRuntimes_.clear(); previousTransitionRevisions_.clear();
    return true;
}
}
