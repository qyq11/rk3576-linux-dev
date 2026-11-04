#include "original_lis_application_plan.h"
#include "original_lis_download_codec.h"

#include <QVector>
#include <limits>
#include <string>

namespace Sn100Lis
{
namespace
{
class TextBudget
{
public:
    explicit TextBudget(qint64 maximum) : maximum_(maximum) {}
    bool addBytes(qint64 bytes)
    {
        if (bytes < 0 || bytes > maximum_ - used_) return false;
        used_ += bytes;
        return true;
    }
    bool add(const QString &text) { return addBytes(qint64(text.size()) * 2); }
    bool replace(const QString &before, const QString &after)
    {
        const qint64 retained = used_ - qint64(before.size()) * 2;
        const qint64 added = qint64(after.size()) * 2;
        if (retained < 0 || added > maximum_ - retained) return false;
        used_ = retained + added;
        return true;
    }
    qint64 used() const { return used_; }
private:
    qint64 maximum_ = 0, used_ = 0;
};

bool addIdentity(TextBudget &budget, const ConfigurationIdentity &identity)
{
    return budget.add(identity.effectiveKey) && budget.add(identity.transportToken)
        && budget.add(identity.sourceFingerprint);
}

bool addProfile(TextBudget &budget, const ReceiveProfilePtr &profile)
{
    return profile && addIdentity(budget, profile->configuration())
        && budget.add(profile->sequenceNumber()) && budget.add(profile->originalRoot());
}

bool addPatient(TextBudget &budget, const OriginalLisPatientValue &patient)
{
    return budget.add(patient.bedNumber) && budget.add(patient.name) && budget.add(patient.barcode)
        && budget.add(patient.remark) && budget.add(patient.checker) && budget.add(patient.department);
}

bool addHole(TextBudget &budget, const OriginalLisHoleValue &hole)
{
    return budget.add(hole.originalRoot) && budget.add(hole.rackInstance) && budget.add(hole.rack)
        && budget.add(hole.internalSampleNumber) && budget.add(hole.barcode) && addPatient(budget, hole.patient);
}

bool addDecoded(TextBudget &budget, const OriginalLisDsrFields &decoded)
{
    const auto &header = decoded.header;
    const auto &source = header.source;
    if (!budget.addBytes(source.body.size()) || !budget.add(source.text)
        || !addIdentity(budget, source.configuration) || !addProfile(budget, source.profile)) return false;
    for (const auto &segment : header.segments) if (!budget.add(segment)) return false;
    const QStringList headerText{header.sequenceNumberSnapshot, header.sendingApplication, header.messageType,
        header.controlId, header.version, header.applicationAcknowledgmentType, header.responseCode,
        header.responseControlId, header.error.field, header.error.detail};
    for (const auto &text : headerText) if (!budget.add(text)) return false;
    const auto &patient = decoded.patient;
    const QStringList patientText{patient.patientIdentifier, patient.bedNumber, patient.name, patient.wardAndRoom,
        patient.ward, patient.room, patient.sex, patient.ageAndUnit, patient.age, patient.ageUnit};
    for (const auto &text : patientText) if (!budget.add(text)) return false;
    const auto &order = decoded.order;
    const QStringList orderText{order.barcode, order.sampleNumber, order.urgent, order.registrationTime,
        order.repeatTimes, order.rackAndHole, order.rack, order.hole, order.dilution, order.remark,
        order.specimenReceivedTime, order.specimenType, order.checker, order.department};
    for (const auto &text : orderText) if (!budget.add(text)) return false;
    if (!budget.add(decoded.declaredProjectCount) || !budget.add(decoded.projectText)
        || !budget.add(decoded.error.field) || !budget.add(decoded.error.detail)) return false;
    for (const auto &project : decoded.projects)
        if (!budget.add(project.code) || !budget.add(project.countText)) return false;
    return true;
}

bool addProject(TextBudget &budget, const OriginalLisMappingProject &project)
{
    if (!budget.add(project.name) || !budget.add(project.informationName) || !budget.add(project.originalFile)
        || !budget.add(project.originalFingerprint)) return false;
    for (const auto &code : project.resultLisCodes) if (!budget.add(code)) return false;
    for (const auto &rate : project.sampleRates) if (!budget.add(rate.rate)) return false;
    return true;
}

OriginalLisApplicationPlan rejected(const QString &field, const QString &detail)
{
    OriginalLisApplicationPlan result;
    result.error.field = field;
    result.error.detail = detail;
    return result; // 不把已算出的部分患者/任务当成可应用的半份计划。
}

bool asciiSpace(ushort value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n' || value == '\f' || value == '\v';
}

bool originalAge(const QString &text, qint32 *value)
{
    // 原atoi对UTF8 C串只读取ASCII十进制前缀，NUL/其他字节结束；无数字为0。
    // QString的非ASCII字符在UTF8中也不会变成ASCII数字/空白，因此不必先分配UTF8副本。
    int index = 0;
    while (index < text.size() && asciiSpace(text.at(index).unicode())) ++index;
    bool negative = false;
    if (index < text.size() && (text.at(index) == QChar('+') || text.at(index) == QChar('-')))
    { negative = text.at(index) == QChar('-'); ++index; }
    const quint32 limit = negative ? quint32(2147483648u) : quint32(2147483647u);
    quint32 magnitude = 0;
    while (index < text.size())
    {
        const ushort character = text.at(index).unicode();
        if (character < '0' || character > '9') break;
        const quint32 digit = character - '0';
        if (magnitude > (limit - digit) / 10) return false;
        magnitude = magnitude * 10 + digit;
        ++index;
    }
    *value = qint32(negative ? -qint64(magnitude) : qint64(magnitude));
    return true;
}

qint32 originalSampleNumber(const QString &text)
{
    // 原MinGW32的toULong与Qt toUInt同为32位无符号范围，失败均为0。
    // 合法高半区按原Win32二进制补码落int32，避免ARM64 unsigned long变成64位。
    const quint32 value = text.toUInt();
    return value <= quint32(2147483647u) ? qint32(value) : qint32(qint64(value) - qint64(4294967296LL));
}

OriginalLisPatientChanges patientChanges(const OriginalLisPatientValue &before, const OriginalLisPatientValue &after)
{
    OriginalLisPatientChanges result;
    result.bedNumber = before.bedNumber != after.bedNumber; result.name = before.name != after.name;
    result.sex = before.sex != after.sex; result.age = before.age != after.age;
    result.sampleNumber = before.sampleNumber != after.sampleNumber; result.barcode = before.barcode != after.barcode;
    result.registrationTime = before.registrationTime != after.registrationTime; result.remark = before.remark != after.remark;
    result.checker = before.checker != after.checker; result.department = before.department != after.department;
    return result;
}
}

OriginalLisApplicationPlan buildOriginalLisApplicationPlan(const OriginalLisDsrFields &decoded,
    const OriginalLisHoleValue &oldHole, const OriginalLisMappingProjects &projects)
{
    const auto &header = decoded.header;
    const auto &source = header.source;
    if (decoded.state != OriginalLisDsrState::FieldsReady || header.state != OriginalLisHeaderState::Ready
        || header.kind != OriginalLisReplyKind::Dsr || header.responseCode != QStringLiteral("OK"))
        return rejected(QStringLiteral("decoded"), QStringLiteral("输入不是原成功DSR字段值；未把拒绝、非OK或其他消息变成空任务成功。"));
    if (!source.profile || !source.profile->matches(source.configuration)
        || source.profile->sequenceNumber() != header.sequenceNumberSnapshot
        || source.profile->originalRoot() != oldHole.originalRoot)
        return rejected(QStringLiteral("source"), QStringLiteral("字段、原配置快照或原资料根不一致；没有改读当前配置。"));
    if (source.body.size() > OriginalLisMaximumBodyBytes || source.text.size() > OriginalLisMaximumTextCodeUnits
        || header.segments.size() < 5 || header.segments.size() > OriginalLisMaximumSegments
        || decoded.projects.size() > OriginalLisMaximumProjectItems)
        return rejected(QStringLiteral("decoded"), QStringLiteral("输入超出冻结字段层的结构预算，未因FieldsReady枚举而跳过大小检查。"));
    if (projects.size() > OriginalLisMaximumMappingProjects)
        return rejected(QStringLiteral("projects"), QStringLiteral("有序映射项目超过本地4096项预算。"));
    qint64 mappingItems = 0;
    for (const auto &project : projects)
    {
        const qint64 items = qint64(project.resultLisCodes.size()) + qint64(project.sampleRates.size());
        if (items > OriginalLisMaximumMappingItems - mappingItems)
            return rejected(QStringLiteral("projects"), QStringLiteral("结果代码与稀释项合计超过本地16384项预算。"));
        mappingItems += items;
    }
    // 输入一账：项目快照、旧孔、解码文字及原正文都保守累计；共享副本也按逻辑内容计费。
    TextBudget input(OriginalLisMaximumPlanInputBytes);
    if (!addHole(input, oldHole) || !addDecoded(input, decoded))
        return rejected(QStringLiteral("input"), QStringLiteral("旧孔或解码输入超过本地合计4MiB文字/原正文预算。"));
    for (const auto &project : projects)
        if (!addProject(input, project))
            return rejected(QStringLiteral("input"), QStringLiteral("项目快照加入后超过本地输入合计4MiB预算。"));

    // 一次索引，保原名称首匹配、无名称匹配时结果代码的末匹配；索引不决定遍历顺序。
    QMap<QString, int> nameFirst, codeLast, informationFirst;
    QVector<QString> defaultRates(projects.size());
    for (int index = 0; index < projects.size(); ++index)
    {
        const auto &project = projects.at(index);
        if (!nameFirst.contains(project.name)) nameFirst.insert(project.name, index);
        if (!informationFirst.contains(project.informationName)) informationFirst.insert(project.informationName, index);
        for (const auto &code : project.resultLisCodes) codeLast.insert(code, index);
        for (const auto &rate : project.sampleRates)
            if (rate.defaultOption) { defaultRates[index] = rate.rate; break; }
    }

    OriginalLisApplicationPlan plan;
    plan.configuration = source.configuration; plan.profile = source.profile;
    plan.generation = source.generation; plan.peer = source.peer; plan.controlId = header.controlId;
    plan.after = oldHole;
    auto &patient = plan.after.patient;
    patient.bedNumber = decoded.patient.bedNumber; patient.name = decoded.patient.name;
    patient.sex = decoded.patient.sex == QStringLiteral("M") ? 0 : decoded.patient.sex == QStringLiteral("F") ? 1 : -1;
    if (!originalAge(decoded.patient.age, &patient.age))
        return rejected(QStringLiteral("PID.30"), QStringLiteral("年龄十进制前缀超出原int32可表示范围；原atoi无可靠成功语义，本片安全拒绝，不钳位或补0。"));
    patient.sampleNumber = originalSampleNumber(decoded.order.sampleNumber);
    patient.barcode = decoded.order.barcode;
    patient.registrationTime = QDateTime::fromString(decoded.order.registrationTime);
    patient.remark = decoded.order.remark; patient.checker = decoded.order.checker;
    patient.department = decoded.order.department;
    plan.patientChanged = patientChanges(oldHole.patient, patient);
    int repeats = decoded.order.repeatTimes.toInt();
    if (repeats < 1) repeats = 1; // 原Qt整串失败/越界返回0，也按此变成1。

    // 先计实际旧孔；命中时在赋值前更换这两笔费用，全未知时不计未发生的序号/条码变化。
    const QString returnedNumber = QString::number(patient.sampleNumber);
    TextBudget output(OriginalLisMaximumPlanOutputBytes);
    if (!addIdentity(output, plan.configuration) || !addProfile(output, plan.profile)
        || !output.add(plan.controlId) || !addHole(output, plan.after))
        return rejected(QStringLiteral("output"), QStringLiteral("患者、旧孔或来源文字超过本地输出4MiB预算。"));

    std::string previousCode;
    bool firstCode = true;
    for (const auto &item : decoded.projects)
    {
        const std::string code = item.code.toStdString();
        if (!firstCode && !(previousCode < code))
            return rejected(QStringLiteral("DSP.3"), QStringLiteral("输入分项不是冻结codec的唯一UTF8字节顺序，未重新排序或覆盖手工改写值。"));
        previousCode = code; firstCode = false;
        auto named = nameFirst.constFind(item.code);
        int selected = -1;
        if (named != nameFirst.cend()) selected = named.value();
        else
        {
            const auto coded = codeLast.constFind(item.code);
            if (coded != codeLast.cend()) selected = coded.value();
        }
        if (selected < 0 || projects.at(selected).name.isEmpty())
        { ++plan.ignoredUnknownProjects; continue; }
        const QString &name = projects.at(selected).name;
        if (plan.tasks.contains(name)) { ++plan.ignoredDuplicateProjects; continue; }
        // 原另按projectBase[project_en_name]首查资料；不把它暗当project_name字段。
        const auto foundInformation = informationFirst.constFind(name);
        const int information = foundInformation == informationFirst.cend() ? -1 : foundInformation.value();
        OriginalLisPlannedTask task;
        task.projectName = name; task.repeats = repeats;
        if (information >= 0)
        {
            const auto &project = projects.at(information);
            task.projectInformationFound = true; task.projectId = project.projectId;
            task.originalFile = project.originalFile; task.originalFingerprint = project.originalFingerprint;
            task.defaultRate = defaultRates.at(information);
        } // 未查到时保留原空ProjectInformation的默认稀释/元数据，不替换为别的项目。
        if (plan.tasks.size() >= OriginalLisMaximumPlannedTasks)
            return rejected(QStringLiteral("tasks"), QStringLiteral("输出任务计划超过本地4096项预算。"));
        if (plan.after.internalSampleNumber != returnedNumber)
        {
            if (!output.replace(plan.after.internalSampleNumber, returnedNumber))
                return rejected(QStringLiteral("output"), QStringLiteral("原孔内部序号更新后超过本地输出4MiB预算。"));
            plan.after.internalSampleNumber = returnedNumber; plan.holeAssigned.internalSampleNumber = true;
        }
        if (plan.after.barcode != patient.barcode)
        {
            if (!output.replace(plan.after.barcode, patient.barcode))
                return rejected(QStringLiteral("output"), QStringLiteral("原孔条码更新后超过本地输出4MiB预算。"));
            plan.after.barcode = patient.barcode; plan.holeAssigned.barcode = true;
        }
        if (!output.add(name) || !output.add(task.projectName) || !output.add(task.originalFile)
            || !output.add(task.originalFingerprint) || !output.add(task.defaultRate))
            return rejected(QStringLiteral("output"), QStringLiteral("项目任务加入后超过本地输出4MiB文字预算。"));
        if (plan.scalarRepeatTotal > std::numeric_limits<qint64>::max() - qint64(repeats))
            return rejected(QStringLiteral("tasks"), QStringLiteral("重复次数标量总数不可表示，未展开或截短。"));
        plan.tasks.insert(name, task); plan.scalarRepeatTotal += repeats;
        // 这些赋值原本位于实际命中且未重复的循环内；全未知时一个也不发生。
        if (header.applicationAcknowledgmentType.contains(QStringLiteral("0")))
        { plan.after.sampleType = 0; plan.holeAssigned.sampleType = true; }
        plan.after.needleType = 1; plan.holeAssigned.needleType = true;
        plan.after.bottleType = 0; plan.holeAssigned.bottleType = true;
    }
    // 原setHoleTask始终替换，随后从新任务计算孔状态；空列表为None，非空Idle任务为Ready。
    plan.after.taskState = plan.tasks.isEmpty() ? OriginalLisHoleTaskState::None : OriginalLisHoleTaskState::Ready;
    plan.holeAssigned.taskState = true;
    plan.holeChanged.sampleType = oldHole.sampleType != plan.after.sampleType;
    plan.holeChanged.needleType = oldHole.needleType != plan.after.needleType;
    plan.holeChanged.bottleType = oldHole.bottleType != plan.after.bottleType;
    plan.holeChanged.internalSampleNumber = oldHole.internalSampleNumber != plan.after.internalSampleNumber;
    plan.holeChanged.barcode = oldHole.barcode != plan.after.barcode;
    plan.holeChanged.taskState = oldHole.taskState != plan.after.taskState;
    plan.assignPatientFields = true; plan.replaceAllTasks = true;
    plan.describeMaxAfterMemory = true; plan.describeAckRequestAfterMax = true;
    plan.inputTextBytes = input.used(); plan.outputTextBytes = output.used();
    plan.state = OriginalLisPlanState::Prepared;
    return plan;
}
}
