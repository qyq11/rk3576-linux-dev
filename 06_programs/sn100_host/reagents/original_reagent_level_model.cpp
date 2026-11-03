#include "original_reagent_level.h"
#include <QRegularExpression>
#include <QSet>
#include <cmath>
#include <limits>

namespace OriginalLevels
{
namespace
{
const QString informationFile=QStringLiteral("db/userSet/reagent_Information.db");
const QString bottleFile=QStringLiteral("db/systemInfo/reagent_Bottle_Type.db");
const QString settingFile=QStringLiteral("db/userSet/reagent_Setting.db");

bool fail(QString *message, const QString &reason)
{
    if (message) *message=reason;
    return false;
}

bool finiteNumber(const QVariant &value, double *number)
{
    bool ok=false;
    *number=value.toString().toDouble(&ok);
    return ok && std::isfinite(*number);
}

QString canonicalArea(const QString &area)
{
    if (area==QStringLiteral("0") || area==QStringLiteral("s_area")) return QStringLiteral("s_area");
    if (area==QStringLiteral("1") || area==QStringLiteral("r_area")) return QStringLiteral("r_area");
    return QString();
}

QString canonicalBottle(const QString &bottle)
{
    const QStringList aliases{QStringLiteral("7mL"),QStringLiteral("10mL"),QStringLiteral("15mL"),
                              QStringLiteral("30mL"),QStringLiteral("50mL")};
    for (int i=0;i<aliases.size();++i) if (bottle==QString::number(i+1)) return aliases[i];
    bool numeric=false;
    bottle.toDouble(&numeric);
    return numeric ? QString() : bottle;
}

bool knownMaximumSource(MaximumSource source)
{
    switch (source)
    {
    case MaximumSource::InitialBottleByName:
    case MaximumSource::ConfirmedUr:
    case MaximumSource::ManualHoleSave:
    case MaximumSource::FilledHole:
    case MaximumSource::ClearedHole: return true;
    case MaximumSource::Unknown: return false;
    }
    return false;
}

bool samePacket(const ReceivedPacket &first, const ReceivedPacket &second)
{
    return first.kind==second.kind && first.linuxNew==second.linuxNew && first.payload==second.payload &&
        first.rawFields==second.rawFields && first.fields==second.fields && first.hole==second.hole &&
        first.reagent==second.reagent && first.area==second.area && first.bottle==second.bottle &&
        first.sendIndex==second.sendIndex && first.reportedDiameter==second.reportedDiameter &&
        first.reportedAngle==second.reportedAngle && first.reportedVolume==second.reportedVolume &&
        first.needle==second.needle && first.rawHeightMm==second.rawHeightMm;
}

bool verifiedProjects(const ProjectSnapshot &projects, const QString &root, QString *message)
{
    if (!projects.orderVerified || projects.versions.value(QStringLiteral("config.ini")).isEmpty())
        return fail(message,QStringLiteral("原项目步骤/项目顺序尚未核实，或排序配置版本缺失。"));
    QSet<QString> files, names;
    QSet<int> orders;
    for (const auto &project : projects.records)
    {
        const QString key=QStringLiteral("project/")+project.originalFileName;
        bool orderOk=false;
        const int order=project.originalBaseValues.value(QStringLiteral("xuhao")).toInt(&orderOk);
        if (project.originalRoot!=root || project.originalFileName.isEmpty() || files.contains(project.originalFileName) ||
            project.originalFingerprint.isEmpty() || projects.versions.value(key)!=project.originalFingerprint ||
            project.name.isEmpty() || project.originalBaseValues.value(QStringLiteral("project_en_name"))!=project.name ||
            !orderOk || project.id!=order || names.contains(project.name) || orders.contains(order))
            return fail(message,QStringLiteral("原项目快照的工作根、文件身份或版本不完整，未猜测项目用量。"));
        files.insert(project.originalFileName);
        names.insert(project.name); orders.insert(order);
    }
    return true;
}

double limitedDisplay(double amount, double maximum)
{
    // 对应原N界面的先上限、后下限判断，不把这个数当作数据库原探测量。
    if (amount>maximum) return maximum;
    if (amount<0) return 0;
    return amount;
}

ReagentTestCount selectedTests(const ProjectSnapshot &projects, const QString &name,
                              double amount, double minimum)
{
    ReagentTestCount result;
    QString text;
    for (const auto &project : projects.records)
    {
        for (const auto &step : project.advanced.reagents)
            if (step.value(QStringLiteral("reagent_name"))==name)
            {
                text=step.value(QStringLiteral("reagent_amount_ul"));
                break; // 原版每个项目只看首个同名步骤，空文字才接着看下一项目。
            }
        if (!text.isEmpty()) break;
    }
    double use=0;
    if (!finiteNumber(text,&use) || use<=0)
    {
        result.reason=QStringLiteral("没有可确认的首个同名项目单次用量，剩余次数未知。匹配不到用量不等于0次。");
        return result;
    }
    const double tests=std::floor((amount-minimum)/use);
    if (!std::isfinite(tests) || tests>std::numeric_limits<int>::max())
    {
        result.reason=QStringLiteral("按原项目用量得到的次数超出整数范围，未截断显示。");
        return result;
    }
    result.available=true;
    result.tests=tests<=0 ? 0 : static_cast<int>(tests);
    return result;
}
}

bool prepareObservation(const OriginalReagentState &state, const ReceivedPacket &packet,
                        const Context &context, const CurrentHoleRuntime &runtime,
                        const ProjectSnapshot &projects, PreparedObservation *result, QString *message)
{
    if (message) message->clear();
    if (!result) return fail(message,QStringLiteral("N液位准备结果输出为空。"));
    *result=PreparedObservation();
    if (!context.packetId || !context.connection || !context.permissionRevision || context.root.isEmpty() ||
        context.actualModel.isEmpty() || context.actualModel!=context.configuredModel || context.themeFingerprint.isEmpty() ||
        context.linuxNew!=(context.protocolToken==QStringLiteral("LinuxNew")) ||
        (context.configuredPositions!=20 && context.configuredPositions!=42) ||
        context.configuredPositions!=context.reportedPositions || context.mainStatus<0 || context.mainStatus>6 ||
        context.capacityCalibrationActive)
        return fail(message,QStringLiteral("N到达连接、许可、机型、20/42孔配置、当前状态或标定模式尚未核实。"));
    const QString expectedTable=context.configuredPositions==20 ? QStringLiteral("reagent_setting") : QStringLiteral("reagent_setting_6600");
    if (state.root!=context.root || state.settingTable!=expectedTable || runtime.root!=context.root ||
        packet.linuxNew!=context.linuxNew)
        return fail(message,QStringLiteral("N快照工作根、原孔位表或新旧协议与接收上下文不一致。"));
    ReceivedPacket verified;
    if (!decodePacket(packet.payload,context.linuxNew,&verified,message)) return false;
    if (verified.kind!=PacketKind::Reagent)
        return fail(message,QStringLiteral("N样本液位已经分流，不进入试剂换算或保存。"));
    if (!samePacket(packet,verified))
        return fail(message,QStringLiteral("N已解析字段与保留的原报文不一致，未使用漂移的输入。"));
    const QRegularExpression visible(context.configuredPositions==42 ?
        QStringLiteral("^([A-F]_[1-7]|SD_[1-4])$") : QStringLiteral("^(R_([1-9]|1[0-9]|20)|SD_[1-4])$"));
    if (!visible.match(packet.hole).hasMatch())
        return fail(message,QStringLiteral("N孔位不属于当前实际20或42位页面，未处理表中额外行。"));
    if (!runtime.available || !runtime.revision || !knownMaximumSource(runtime.maximumSource) ||
        !std::isfinite(runtime.maximumMicrolitres) || runtime.maximumMicrolitres<0 ||
        runtime.maximumMicrolitres>std::numeric_limits<int>::max() ||
        !std::isfinite(runtime.amountMicrolitres) || !std::isfinite(runtime.usedMicrolitres))
        return fail(message,QStringLiteral("本孔独立运行快照、最大量来源或版本未确认，不能借用孔库max_ul。"));
    if (state.versions.value(informationFile).isEmpty() || state.versions.value(bottleFile).isEmpty() ||
        state.versions.value(settingFile).isEmpty() ||
        runtime.informationFingerprint!=state.versions.value(informationFile) ||
        runtime.bottleFingerprint!=state.versions.value(bottleFile) ||
        runtime.settingFingerprint!=state.versions.value(settingFile))
        return fail(message,QStringLiteral("N运行快照与原试剂、瓶型、孔位三库版本不一致。"));
    if (!verifiedProjects(projects,context.root,message)) return false;
    const OriginalBusiness::Row *hole=nullptr;
    for (const auto &row : state.holes)
        if (row.value(QStringLiteral("hole_name")).toString()==packet.hole)
        {
            if (hole) return fail(message,QStringLiteral("原孔位表有重名孔，未合并N液位。"));
            hole=&row;
        }
    if (!hole) return fail(message,QStringLiteral("N试剂孔位不存在于当前原孔位表。"));
    bool idOk=false;
    const qint64 holeId=hole->value(QStringLiteral("id")).toLongLong(&idOk);
    const QString reagent=hole->value(QStringLiteral("reagent_name")).toString();
    const QString bottle=hole->value(QStringLiteral("bottle_type")).toString();
    const QString area=hole->value(QStringLiteral("area")).toString();
    if (!idOk || holeId<=0 || !hole->contains(QStringLiteral("used_ul")) ||
        reagent!=packet.reagent || bottle.isEmpty() || bottle!=canonicalBottle(packet.bottle) ||
        area.isEmpty() || area!=canonicalArea(packet.area) ||
        runtime.hole!=packet.hole || runtime.reagent!=reagent || runtime.bottle!=bottle || runtime.area!=area ||
        runtime.reagentType!=hole->value(QStringLiteral("reagent_type")).toString())
        return fail(message,QStringLiteral("N孔位、试剂、瓶型或区域与当前原库及独立运行快照不对应。"));
    const QStringList states{QStringLiteral("r_none"),QStringLiteral("r_normal"),QStringLiteral("r_warning"),
                              QStringLiteral("r_useout"),QStringLiteral("r_unknown"),QStringLiteral("r_outdate")};
    if (!states.contains(runtime.state))
        return fail(message,QStringLiteral("本孔当前界面状态未识别，未猜测N更新前状态。"));
    const OriginalBusiness::Row *definition=nullptr, *geometry=nullptr;
    for (const auto &row : state.reagents)
        if (row.value(QStringLiteral("reagent_name")).toString()==reagent)
        {
            if (definition) return fail(message,QStringLiteral("原试剂资料存在重名定义，未猜测液量阈值。"));
            definition=&row;
        }
    if (!definition) return fail(message,QStringLiteral("N当前试剂没有原reagent_list定义。"));
    double out=0, warning=0;
    if (!finiteNumber(definition->value(QStringLiteral("out_ul")),&out) ||
        !finiteNumber(definition->value(QStringLiteral("warning_ul")),&warning))
        return fail(message,QStringLiteral("当前原试剂的缺量或警告阈值不是有限数。"));
    for (const auto &row : state.bottles)
        if (row.value(QStringLiteral("bottle_type_name")).toString()==bottle && row.value(QStringLiteral("area")).toString()==area)
        {
            if (geometry) return fail(message,QStringLiteral("当前区域的原瓶型有重名资料，未猜测几何参数。"));
            geometry=&row;
        }
    PreparedObservation prepared;
    prepared.context=context; prepared.received=verified; prepared.before=runtime; prepared.after=runtime;
    prepared.observed=state; prepared.projects=projects; prepared.holeId=holeId;
    auto &input=prepared.capacityInput;
    input.hole=packet.hole; input.needle=packet.needle; input.area=packet.area;
    input.reportedBottle=packet.bottle; input.rawHeightMm=packet.rawHeightMm;
    input.configuredBottle.hole=packet.hole; input.configuredBottle.name=bottle; input.configuredBottle.area=area;
    input.configuredBottle.maximumMicrolitres=runtime.maximumMicrolitres;
    // 几何缺项保持NaN；有效矩阵不需要它们，真正几何后备由v2明确检查。
    double number=0;
    if (geometry && finiteNumber(geometry->value(QStringLiteral("bottom_diameter")),&number))
        input.configuredBottle.bottomDiameterMm=number;
    if (geometry && finiteNumber(geometry->value(QStringLiteral("angle")),&number))
        input.configuredBottle.angleDegrees=number;
    if (!OriginalRuntime::calculateReagentCapacity(context.root,context.actualModel,input,&prepared.capacity,message)) return false;
    if (context.capacityRelativeFile!=prepared.capacity.relativeFile || context.capacityFileState!=prepared.capacity.fileState ||
        context.capacityFingerprint!=prepared.capacity.fingerprint)
        return fail(message,QStringLiteral("N到达时绑定的容量资料状态或版本已变化，未给旧观测换用新参数。"));
    const double measured=prepared.capacity.volumeMicrolitres;
    if (!prepared.capacity.available || !std::isfinite(measured))
        return fail(message,QStringLiteral("N容量结果不可用，未准备默认0液量。"));
    prepared.formulaText=QString::number(packet.rawHeightMm);
    prepared.amountText=QString::number(measured);
    double stored=0;
    if (!finiteNumber(prepared.amountText,&stored) || stored<std::numeric_limits<int>::min() || stored>std::numeric_limits<int>::max())
        return fail(message,QStringLiteral("N原默认精度液量文本超出原整数amount_uL范围，未截断溢出。"));
    prepared.amountInteger=static_cast<int>(stored);
    prepared.previousUsedText=hole->value(QStringLiteral("used_ul")).toString();
    prepared.updateUsed=context.mainStatus!=5;
    prepared.usedText=prepared.updateUsed ? prepared.amountText : prepared.previousUsedText;
    double displayed=measured;
    if (!prepared.updateUsed)
    {
        // 原Testing从原探测量依次减所有项目每个同名步骤；不能再套assignTask扣量。
        for (const auto &project : projects.records)
            for (const auto &step : project.advanced.reagents)
                if (step.value(QStringLiteral("reagent_name"))==reagent)
                {
                    double use=0;
                    if (!finiteNumber(step.value(QStringLiteral("reagent_amount_ul")),&use) || use<0)
                        return fail(message,QStringLiteral("测试态同名项目步骤用量无效，未猜测N界面扣量。"));
                    displayed-=use;
                    if (!std::isfinite(displayed)) return fail(message,QStringLiteral("测试态N界面扣量超出有限数范围。"));
                    displayed=displayed>=0 ? displayed : 0.0;
                }
        displayed=limitedDisplay(displayed,runtime.maximumMicrolitres);
        prepared.after.amountMicrolitres=displayed;
        if (runtime.usedMicrolitres>displayed) prepared.after.usedMicrolitres=displayed;
    }
    else
    {
        displayed=limitedDisplay(displayed,runtime.maximumMicrolitres);
        prepared.after.amountMicrolitres=displayed;
        prepared.after.usedMicrolitres=displayed;
    }
    prepared.after.state=displayed<=out ? QStringLiteral("r_useout") :
        displayed<=warning ? QStringLiteral("r_warning") : QStringLiteral("r_normal");
    // 原N只重判控件状态；数据库state不在保存字段中，提交前不伪造新setting版本。
    prepared.after.settingFingerprint.clear();
    prepared.selectedHoleTests=selectedTests(projects,reagent,displayed,out);
    *result=prepared;
    return true;
}
}
