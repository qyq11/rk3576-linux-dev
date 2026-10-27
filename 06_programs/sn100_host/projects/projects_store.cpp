#include "projects_store.h"
#include "original_project_repository.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
const char markerName[] = ".sn100-catalog-offline-v1";
QString sqlError(const QSqlQuery &query)
{
    return query.lastError().text();
}
bool validName(const QString &name, int length)
{
    // Reference helper's ASCII head/exact-length regex conflicts with the
    // Chinese full-name field. This isolated slice accepts a Unicode letter or
    // digit head, at most the stated length, and excludes path characters.
    static const QRegularExpression expression(QStringLiteral("^[\\p{L}\\p{N}][\\p{L}\\p{N}_() .%+\\-]*$"));
    return !name.isEmpty() && name.size() <= length && name == name.trimmed() &&
           expression.match(name).hasMatch();
}
QString validateUnit(const ProjectUnit &unit)
{
    if (unit.name.isEmpty() || unit.name.size() > 64 || unit.name != unit.name.trimmed())
        return QStringLiteral("单位名称须为1至64字符，不能有首尾空格。");
    for (const QChar character : unit.name)
        if (character.isNull() || character.category() == QChar::Other_Control ||
            character == QLatin1Char('/') || character == QLatin1Char('\\'))
            return QStringLiteral("单位名称不能含控制字符或路径字符。");
    if (!std::isfinite(unit.ratio) || unit.ratio < 1 ||
        unit.ratio > std::numeric_limits<int>::max() || std::floor(unit.ratio) != unit.ratio)
        return QStringLiteral("单位倍率须为1至2147483647的整数，不能截断小数或无效值。");
    return {};
}
int resultSlot(const QString &type)
{
    return QStringList{QString(), QStringLiteral("Original"), QStringLiteral("Calibration"),
                       QStringLiteral("Rate"), QStringLiteral("INR")}.indexOf(type);
}
QString validateAlgorithm(const ProjectAlgorithm &algorithm)
{
    if (!algorithm.configured)
        return algorithm.coefficientA != 0 || algorithm.coefficientB != 0 || algorithm.coefficientC != 0 || algorithm.autoApply
            ? QStringLiteral("未配置的算法不能携带隐藏系数或自动应用开关。") : QString();
    for (double coefficient : {algorithm.coefficientA, algorithm.coefficientB, algorithm.coefficientC})
        if (!std::isfinite(coefficient) || coefficient < -9999 || coefficient > 9999 ||
            std::abs(coefficient * 10000 - std::round(coefficient * 10000)) > 0.000001)
            return QStringLiteral("系数A、B、C须为-9999至9999的有限数值，最多四位小数。");
    return {};
}
bool ensureResultSelections(QSqlDatabase database, QString *message)
{
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("SELECT value FROM catalog_metadata WHERE key='current_format_schema'")))
    { *message = sqlError(query); return false; }
    const bool marked = query.next();
    if (query.lastError().isValid()) { *message = sqlError(query); return false; }
    if (marked && query.value(0).toString() != QStringLiteral("sn100-current-result-format-source-v1"))
    { *message = QStringLiteral("结果选择配置版本未知，保留原数据。"); return false; }
    query.finish();
    if (database.tables().contains(QStringLiteral("project_result_format_source")) != marked)
    { *message = QStringLiteral("结果选择表和版本标记不一致，未接管或迁移旧表。"); return false; }
    const QStringList schema{
        QStringLiteral("CREATE TABLE IF NOT EXISTS project_result_format_source(project_id INTEGER NOT NULL REFERENCES project_base(id) ON DELETE CASCADE,result_type TEXT NOT NULL,result_id INTEGER NOT NULL CHECK(result_id BETWEEN 1 AND 4),is_used INTEGER NOT NULL CHECK(is_used IN(0,1)),display INTEGER NOT NULL CHECK(display IN(0,1)),PRIMARY KEY(project_id,result_type))"),
        QStringLiteral("INSERT OR IGNORE INTO catalog_metadata(key,value) VALUES('current_format_schema','sn100-current-result-format-source-v1')")};
    for (const QString &statement : schema)
        if (!query.exec(statement)) { *message = sqlError(query); return false; }
    return true;
}
QJsonObject fieldsJson(const QMap<QString, QString> &fields)
{
    QJsonObject object;
    for (auto it = fields.cbegin(); it != fields.cend(); ++it)
    {
        object.insert(it.key(), it.value());
    }
    return object;
}
QMap<QString, QString> jsonFields(const QJsonObject &object)
{
    QMap<QString, QString> fields;
    for (auto it = object.begin(); it != object.end(); ++it)
    {
        fields.insert(it.key(), it.value().toString());
    }
    return fields;
}
QString advancedJson(const ProjectAdvanced &advanced)
{
    QJsonObject object;
    object.insert(QStringLiteral("fields"), fieldsJson(advanced.fields));
    QJsonArray samples, reagents;
    for (const auto &row : advanced.samples) samples.append(fieldsJson(row));
    for (const auto &row : advanced.reagents) reagents.append(fieldsJson(row));
    object.insert(QStringLiteral("samples"), samples);
    object.insert(QStringLiteral("reagents"), reagents);
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}
bool parseAdvanced(const QString &json, ProjectAdvanced *advanced)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return false;
    const auto object = document.object();
    if (object.size() != 3 || !object.value(QStringLiteral("fields")).isObject() ||
        !object.value(QStringLiteral("samples")).isArray() ||
        !object.value(QStringLiteral("reagents")).isArray()) return false;
    advanced->configured = true;
    auto stringFields = [](const QJsonObject &fields)
    {
        for (auto field = fields.begin(); field != fields.end(); ++field)
            if (!field.value().isString()) return false;
        return true;
    };
    if (!stringFields(object.value(QStringLiteral("fields")).toObject())) return false;
    advanced->fields = jsonFields(object.value(QStringLiteral("fields")).toObject());
    for (const auto &row : object.value(QStringLiteral("samples")).toArray())
    {
        if (!row.isObject() || !stringFields(row.toObject())) return false;
        advanced->samples.append(jsonFields(row.toObject()));
    }
    for (const auto &row : object.value(QStringLiteral("reagents")).toArray())
    {
        if (!row.isObject() || !stringFields(row.toObject())) return false;
        advanced->reagents.append(jsonFields(row.toObject()));
    }
    return true;
}
bool exactKeys(const QJsonObject &object, const QStringList &keys)
{
    if (object.size() != keys.size()) return false;
    for (const auto &key : keys) if (!object.contains(key)) return false;
    return true;
}
bool jsonProject(const QJsonObject &object, ProjectRecord *project, QString *message)
{
    auto reject = [&](const QString &reason) { *message = reason; return false; };
    if (!exactKeys(object, {QStringLiteral("instrumentProjectId"), QStringLiteral("name"), QStringLiteral("fullName"),
        QStringLiteral("lisCode"), QStringLiteral("enabled"), QStringLiteral("results"), QStringLiteral("sampleRates"),
        QStringLiteral("advanced"), QStringLiteral("algorithm")}))
        return reject(QStringLiteral("项目字段不完整或含未知字段，未忽略输入。"));
    for (const auto &field : {QStringLiteral("instrumentProjectId"), QStringLiteral("name"), QStringLiteral("fullName"), QStringLiteral("lisCode")})
        if (!object.value(field).isString()) return reject(QStringLiteral("项目文字字段类型错误：") + field);
    if (!object.value(QStringLiteral("enabled")).isBool() || !object.value(QStringLiteral("results")).isArray() ||
        !object.value(QStringLiteral("sampleRates")).isArray()) return reject(QStringLiteral("项目启用、结果或稀释列表类型错误。"));
    project->instrumentProjectId = object.value(QStringLiteral("instrumentProjectId")).toString();
    project->name = object.value(QStringLiteral("name")).toString();
    project->fullName = object.value(QStringLiteral("fullName")).toString();
    project->lisCode = object.value(QStringLiteral("lisCode")).toString();
    project->enabled = object.value(QStringLiteral("enabled")).toBool();
    const QJsonArray results = object.value(QStringLiteral("results")).toArray();
    if (results.isEmpty() || results.size() > 6) return reject(QStringLiteral("每个项目须有1至6个结果配置。"));
    for (const auto &item : results)
    {
        if (!item.isObject()) return reject(QStringLiteral("结果配置必须是对象。"));
        const auto result = item.toObject();
        if (!exactKeys(result, {QStringLiteral("type"), QStringLiteral("name"), QStringLiteral("lisCode"), QStringLiteral("unit"),
            QStringLiteral("decimals"), QStringLiteral("testLow"), QStringLiteral("testHigh"), QStringLiteral("normalLow"),
            QStringLiteral("normalHigh"), QStringLiteral("mainResult"), QStringLiteral("selectionKnown"), QStringLiteral("sourceResultId"),
            QStringLiteral("used"), QStringLiteral("display")})) return reject(QStringLiteral("结果字段不完整或含未知字段。"));
        for (const auto &field : {QStringLiteral("type"), QStringLiteral("name"), QStringLiteral("lisCode"), QStringLiteral("unit")})
            if (!result.value(field).isString()) return reject(QStringLiteral("结果文字字段类型错误：") + field);
        for (const auto &field : {QStringLiteral("decimals"), QStringLiteral("testLow"), QStringLiteral("testHigh"), QStringLiteral("normalLow"), QStringLiteral("normalHigh"), QStringLiteral("sourceResultId")})
            if (!result.value(field).isDouble() || !std::isfinite(result.value(field).toDouble())) return reject(QStringLiteral("结果数值缺失或无效：") + field);
        for (const auto &field : {QStringLiteral("mainResult"), QStringLiteral("selectionKnown"), QStringLiteral("used"), QStringLiteral("display")})
            if (!result.value(field).isBool()) return reject(QStringLiteral("结果开关须为布尔值：") + field);
        const double decimals = result.value(QStringLiteral("decimals")).toDouble();
        const double slot = result.value(QStringLiteral("sourceResultId")).toDouble();
        if (decimals < 1 || decimals > 6 || std::floor(decimals) != decimals || slot < 0 || slot > 4 || std::floor(slot) != slot)
            return reject(QStringLiteral("小数位数或结果编号超出范围，不能截断小数。"));
        ProjectResult value;
        value.type = result.value(QStringLiteral("type")).toString();
        value.name = result.value(QStringLiteral("name")).toString();
        value.lisCode = result.value(QStringLiteral("lisCode")).toString();
        value.unit = result.value(QStringLiteral("unit")).toString();
        value.decimals = static_cast<int>(decimals);
        value.testLow = result.value(QStringLiteral("testLow")).toDouble();
        value.testHigh = result.value(QStringLiteral("testHigh")).toDouble();
        value.normalLow = result.value(QStringLiteral("normalLow")).toDouble();
        value.normalHigh = result.value(QStringLiteral("normalHigh")).toDouble();
        value.mainResult = result.value(QStringLiteral("mainResult")).toBool();
        value.selectionKnown = result.value(QStringLiteral("selectionKnown")).toBool();
        value.sourceResultId = static_cast<int>(slot);
        value.used = result.value(QStringLiteral("used")).toBool();
        value.display = result.value(QStringLiteral("display")).toBool();
        project->results.append(value);
    }
    const auto rates = object.value(QStringLiteral("sampleRates")).toArray();
    if (rates.size() > 64) return reject(QStringLiteral("稀释选项超过64条。"));
    for (const auto &item : rates)
    {
        if (!item.isObject()) return reject(QStringLiteral("稀释选项必须是对象。"));
        const auto rate = item.toObject();
        if (!exactKeys(rate, {QStringLiteral("sampleRate"), QStringLiteral("sampleOption"), QStringLiteral("defaultOption"), QStringLiteral("order")}) ||
            !rate.value(QStringLiteral("sampleRate")).isString() || !rate.value(QStringLiteral("sampleOption")).isBool() ||
            !rate.value(QStringLiteral("defaultOption")).isBool() || !rate.value(QStringLiteral("order")).isDouble())
            return reject(QStringLiteral("稀释选项字段或类型无效。"));
        const double order = rate.value(QStringLiteral("order")).toDouble();
        if (!std::isfinite(order) || order < 1 || order > 64 || std::floor(order) != order)
            return reject(QStringLiteral("稀释选项顺序须为1至64的整数。"));
        project->sampleRates.append({rate.value(QStringLiteral("sampleRate")).toString(), rate.value(QStringLiteral("sampleOption")).toBool(),
            rate.value(QStringLiteral("defaultOption")).toBool(), static_cast<int>(order)});
    }
    const auto advanced = object.value(QStringLiteral("advanced"));
    if (!advanced.isNull())
    {
        if (!advanced.isObject() || !parseAdvanced(QString::fromUtf8(QJsonDocument(advanced.toObject()).toJson(QJsonDocument::Compact)), &project->advanced))
            return reject(QStringLiteral("高级方案格式无效；fields、samples、reagents须齐全，字段值须为字符串。"));
    }
    const auto algorithm = object.value(QStringLiteral("algorithm"));
    if (!algorithm.isNull())
    {
        if (!algorithm.isObject()) return reject(QStringLiteral("算法参数必须是对象或null。"));
        const auto values = algorithm.toObject();
        if (!exactKeys(values, {QStringLiteral("coefficientA"), QStringLiteral("coefficientB"), QStringLiteral("coefficientC"), QStringLiteral("autoApply")}) ||
            !values.value(QStringLiteral("autoApply")).isBool()) return reject(QStringLiteral("算法参数字段或开关类型无效。"));
        for (const auto &field : {QStringLiteral("coefficientA"), QStringLiteral("coefficientB"), QStringLiteral("coefficientC")})
            if (!values.value(field).isDouble()) return reject(QStringLiteral("算法系数须为数值：") + field);
        project->algorithm.configured = true;
        project->algorithm.coefficientA = values.value(QStringLiteral("coefficientA")).toDouble();
        project->algorithm.coefficientB = values.value(QStringLiteral("coefficientB")).toDouble();
        project->algorithm.coefficientC = values.value(QStringLiteral("coefficientC")).toDouble();
        project->algorithm.autoApply = values.value(QStringLiteral("autoApply")).toBool();
    }
    return true;
}
bool writeActionReferences(QSqlDatabase database, qint64 projectId, const ProjectAdvanced &advanced,
                           QString *message)
{
    const bool haveReagents = database.tables().contains(QStringLiteral("reagent_list"));
    QSqlQuery query(database);
    if (haveReagents)
    {
        if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS project_action_reagent_reference(project_id INTEGER NOT NULL REFERENCES project_base(id) ON DELETE CASCADE,reagent_id INTEGER NOT NULL REFERENCES reagent_list(id) ON DELETE RESTRICT,PRIMARY KEY(project_id,reagent_id))")))
        {
            *message = sqlError(query);
            return false;
        }
        query.prepare(QStringLiteral("DELETE FROM project_action_reagent_reference WHERE project_id=?"));
        query.addBindValue(projectId);
        if (!query.exec()) { *message = sqlError(query); return false; }
    }
    QList<QPair<QString, QString>> required;
    bool needsDiluent = false;
    for (const auto &row : advanced.samples)
        needsDiluent = needsDiluent || row.value(QStringLiteral("IBS_amount_ul")).toDouble() > 0 ||
            row.value(QStringLiteral("second_IBS_amount_ul")).toDouble() > 0;
    const QString diluent = advanced.fields.value(QStringLiteral("IBS_name"));
    if (needsDiluent && (diluent.isEmpty() || diluent == QStringLiteral("none")))
    { *message = QStringLiteral("加样方案使用了缓冲液量，请明确选择有效缓冲稀释液。"); return false; }
    if (advanced.fields.contains(QStringLiteral("IBS_name")))
        required.append(qMakePair(advanced.fields.value(QStringLiteral("IBS_name")), QStringLiteral("缓冲稀释液")));
    for (const auto &row : advanced.samples)
        required.append(qMakePair(row.value(QStringLiteral("before_clean_name")), QStringLiteral("清洗液")));
    for (const auto &row : advanced.reagents)
    {
        required.append(qMakePair(row.value(QStringLiteral("reagent_name")), QStringLiteral("试剂")));
        required.append(qMakePair(row.value(QStringLiteral("before_clean_name")), QStringLiteral("清洗液")));
    }
    for (const auto &reference : required)
    {
        if (reference.first.isEmpty() || reference.first == QStringLiteral("none")) continue;
        if (!haveReagents)
        {
            *message = QStringLiteral("试剂目录尚未就绪，不能保存具名动作试剂。请先保存试剂资料并刷新项目页。");
            return false;
        }
        query.prepare(QStringLiteral("SELECT id FROM reagent_list WHERE reagent_name=? AND reagent_type=? AND valid=1"));
        query.addBindValue(reference.first);
        query.addBindValue(reference.second);
        if (!query.exec() || !query.next())
        {
            *message = QStringLiteral("动作中的%1“%2”未在有效试剂资料中配置。").arg(reference.second, reference.first);
            return false;
        }
        const qint64 reagentId = query.value(0).toLongLong();
        query.prepare(QStringLiteral("INSERT OR IGNORE INTO project_action_reagent_reference VALUES(?,?)"));
        query.addBindValue(projectId);
        query.addBindValue(reagentId);
        if (!query.exec()) { *message = sqlError(query); return false; }
    }
    return true;
}
} // namespace

ProjectsStore::ProjectsStore(QObject *parent) : QObject(parent) {}
ProjectsStore::~ProjectsStore()
{
    release();
}
void ProjectsStore::registerTypes()
{
    qRegisterMetaType<AccountSession>();
    qRegisterMetaType<ProjectUnit>();
    qRegisterMetaType<ProjectUnits>();
    qRegisterMetaType<ProjectResult>();
    qRegisterMetaType<ProjectResults>();
    qRegisterMetaType<ProjectRecord>();
    qRegisterMetaType<ProjectRecords>();
    qRegisterMetaType<ProjectSampleRate>();
    qRegisterMetaType<ProjectSampleRates>();
    qRegisterMetaType<ProjectAdvanced>();
    qRegisterMetaType<ProjectAlgorithm>();
    qRegisterMetaType<ProjectCurrentFormatSnapshot>();
    qRegisterMetaType<ProjectCurrentFormatSelections>();
}
bool ProjectsStore::canEdit(const AccountSession &session)
{
    return session.authenticated && session.actionReady && (session.role == 4 || session.role == 5);
}
QString ProjectsStore::validate(const ProjectRecord &project, const ProjectUnits &units)
{
    if (!project.originalFileName.isEmpty())
        return OriginalProjectRepository::validate(project, units);
    if (project.instrumentProjectId.isEmpty() || project.instrumentProjectId.size() > 128 ||
        project.instrumentProjectId != project.instrumentProjectId.trimmed() || project.instrumentProjectId.contains(QChar::Null))
        return QStringLiteral("请明确填写仪器项目编号（1至128字符），不能有首尾空格或空字符。");
    const QString algorithmError = validateAlgorithm(project.algorithm);
    if (!algorithmError.isEmpty()) return algorithmError;
    if (!validName(project.name, 16) || !validName(project.fullName, 16))
    {
        return QStringLiteral("项目名称和全称须以文字或数字开头，长度1至16，不能有首尾空格或路径字符。");
    }
    if (project.lisCode.size() > 8 || project.lisCode.contains(QChar::Null))
    {
        return QStringLiteral("项目通道号最长8个字符。");
    }
    if (project.results.isEmpty() || project.results.size() > 6)
    {
        return QStringLiteral("项目须含1至6个结果配置。");
    }
    int mainCount = 0;
    QSet<QString> resultTypes;
    for (const ProjectResult &result : project.results)
    {
        if (resultTypes.contains(result.type) || result.type.isEmpty() || result.type.size() > 32 || result.type.contains(QChar::Null))
        {
            return QStringLiteral("结果类型重复或为空。");
        }
        resultTypes.insert(result.type);
        if (result.selectionKnown && (resultSlot(result.type) < 1 || result.sourceResultId != resultSlot(result.type)))
            return QStringLiteral("已知结果选择须与Original/Calibration/Rate/INR及编号1至4严格对应。");
        if (!result.selectionKnown && (result.sourceResultId != 0 || result.used || result.display))
            return QStringLiteral("未知结果选择不能携带推定的编号、启用或显示状态。");
        if (!validName(result.name, 16) || result.lisCode.size() > 8 || result.lisCode.contains(QChar::Null) || result.decimals < 1 ||
            result.decimals > 6)
        {
            return QStringLiteral("结果名称/传输代号或小数位数无效（小数位1至6）。");
        }
        const double ranges[] = {result.testLow, result.testHigh, result.normalLow, result.normalHigh};
        for (double range : ranges)
        {
            if (!std::isfinite(range) || range < 0 || range > 999)
            {
                return QStringLiteral("结果范围须为0至999的有限数值。");
            }
        }
        if (result.testLow > result.testHigh || result.normalLow > result.normalHigh)
        {
            return QStringLiteral("结果下限不能大于上限。");
        }
        bool found = false;
        for (const ProjectUnit &unit : units)
        {
            if (unit.name == result.unit)
            {
                found = true;
            }
        }
        if (!found)
        {
            return QStringLiteral("结果单位未在已加载单位字典中配置。");
        }
        if (result.mainResult)
        {
            ++mainCount;
        }
    }
    if (mainCount != 1)
    {
        return QStringLiteral("须选择一个主显示单位。");
    }
    if (project.sampleRates.size() > 64) return QStringLiteral("稀释选项最多64条；这是本地存储上限，不是仪器能力。");
    QStringList rates;
    int defaults = 0;
    for (const auto &rate : project.sampleRates)
    {
        if (rate.sampleRate.isEmpty() || rate.sampleRate.size() > 64 || rate.sampleRate != rate.sampleRate.trimmed() ||
            rate.sampleRate.contains(QChar::Null) || rates.contains(rate.sampleRate) || rate.order != rates.size() + 1)
            return QStringLiteral("稀释选项须名称非空且不重复、顺序从1连续排列，名称最多64字符。");
        if (rate.defaultOption)
        {
            if (!rate.sampleOption) return QStringLiteral("默认稀释选项必须启用。");
            ++defaults;
        }
        rates.append(rate.sampleRate);
    }
    if (!rates.isEmpty() && defaults != 1) return QStringLiteral("非空稀释列表须恰好有一个默认选项。");
    for (int row = 0; row < project.advanced.samples.size(); ++row)
    {
        if (row >= project.sampleRates.size() || project.advanced.samples[row].value(QStringLiteral("sample_rate")) != project.sampleRates[row].sampleRate ||
            project.advanced.samples[row].value(QStringLiteral("sample_option")) != (project.sampleRates[row].sampleOption ? QStringLiteral("1") : QStringLiteral("0")))
            return QStringLiteral("加样行名称、顺序和选用状态须与稀释选项一致，请刷新后重新编辑。");
        const auto &sample = project.advanced.samples[row];
        if ((sample.value(QStringLiteral("IBS_amount_ul")).toDouble() > 0 ||
             sample.value(QStringLiteral("second_IBS_amount_ul")).toDouble() > 0) &&
            (project.advanced.fields.value(QStringLiteral("IBS_name")).isEmpty() ||
             project.advanced.fields.value(QStringLiteral("IBS_name")) == QStringLiteral("none")))
            return QStringLiteral("第%1个加样方案使用了缓冲液量，请明确选择有效缓冲稀释液。").arg(row + 1);
    }
    return ProjectAdvancedEditor::validate(project.advanced, rates);
}
void ProjectsStore::fail(qint64 requestId, const QString &operation, const QString &message)
{
    emit error(requestId, operation, message);
}
bool ProjectsStore::check(qint64 requestId, const QString &operation, bool write)
{
    if (QThread::currentThread() != thread())
    {
        fail(requestId, operation, QStringLiteral("项目存储调用线程错误。"));
        return false;
    }
    if (!m_ready)
    {
        fail(requestId, operation, QStringLiteral("项目数据库未就绪。"));
        return false;
    }
    if (write && !canEdit(m_session))
    {
        fail(requestId, operation, QStringLiteral("仅工程师/管理员可在就绪状态修改项目。"));
        return false;
    }
    return true;
}
void ProjectsStore::initialize(QString directory, bool seedDemo)
{
    if (QThread::currentThread() != thread())
    {
        fail(0, QStringLiteral("initialize"), QStringLiteral("线程错误。"));
        return;
    }
    release();
    directory = QDir::cleanPath(directory);
    if (!QDir::isAbsolutePath(directory) ||
        directory.contains(QStringLiteral("sn100_host_reference"), Qt::CaseInsensitive))
    {
        fail(0, QStringLiteral("initialize"), QStringLiteral("仅接受显式隔离数据绝对目录，禁止参考路径。"));
        return;
    }
    for (QFileInfo current(directory); !current.filePath().isEmpty(); current.setFile(current.absolutePath()))
    {
        if (current.isSymLink())
        {
            fail(0, QStringLiteral("initialize"), QStringLiteral("隔离目录不能经过符号链接。"));
            return;
        }
        if (current.absoluteFilePath() == current.absolutePath())
        {
            break;
        }
    }
    QDir dir(directory);
    const bool marked = QFileInfo::exists(dir.filePath(QString::fromLatin1(markerName)));
    if (!marked && dir.exists() &&
        !dir.entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty())
    {
        fail(0, QStringLiteral("initialize"), QStringLiteral("非空未标记目录不允许初始化项目库。"));
        return;
    }
    if (!dir.mkpath(QStringLiteral(".")))
    {
        fail(0, QStringLiteral("initialize"), QStringLiteral("无法建立数据目录。"));
        return;
    }
    const QString databasePath = dir.filePath(QStringLiteral("catalog.db"));
    const bool newDatabase = !QFileInfo::exists(databasePath);
    if (QFileInfo(databasePath).isSymLink())
    {
        fail(0, QStringLiteral("initialize"), QStringLiteral("数据库不能是符号链接。"));
        return;
    }
    m_connectionName = QStringLiteral("Projects-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(databasePath);
    m_database.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=1000"));
    if (!m_database.open())
    {
        fail(0, QStringLiteral("initialize"), m_database.lastError().text());
        release();
        return;
    }
    QSqlQuery query(m_database);
    query.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    const QStringList schema = {
        QStringLiteral("PRAGMA foreign_keys=ON"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS catalog_metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS option_unit(unit TEXT PRIMARY KEY,ratio REAL NOT NULL "
                       "CHECK(ratio>0))"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS project_base(id INTEGER PRIMARY KEY AUTOINCREMENT,project_id TEXT "
            "NOT NULL,project_en_name TEXT NOT NULL UNIQUE COLLATE NOCASE,project_cn_name TEXT NOT "
            "NULL,project_lis_code TEXT NOT NULL DEFAULT '',is_used INTEGER NOT NULL CHECK(is_used "
            "IN(0,1)),fixed INTEGER NOT NULL CHECK(fixed IN(0,1)),change_time TEXT NOT NULL)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS project_sample_rate(project_id INTEGER NOT NULL REFERENCES "
            "project_base(id) ON DELETE CASCADE,sample_rate TEXT NOT NULL,sample_option INTEGER NOT NULL "
            "CHECK(sample_option IN(0,1)),sample_default_option INTEGER NOT NULL CHECK(sample_default_option "
            "IN(0,1)),xuhao INTEGER NOT NULL,PRIMARY KEY(project_id,sample_rate),UNIQUE(project_id,xuhao))"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS project_result(project_id INTEGER NOT NULL REFERENCES "
                       "project_base(id) ON DELETE CASCADE,result_type TEXT NOT NULL,result_name TEXT NOT "
                       "NULL,decimal_point INTEGER NOT NULL,test_low_range REAL NOT NULL,test_up_range REAL "
                       "NOT NULL,normal_low_range REAL NOT NULL,normal_up_range REAL NOT NULL,lis_code TEXT "
                       "NOT NULL,unit_name TEXT NOT NULL REFERENCES option_unit(unit) ON DELETE "
                       "RESTRICT,main_result INTEGER NOT NULL,PRIMARY KEY(project_id,result_type))")};
    if (!newDatabase)
    {
        if (!query.exec(QStringLiteral("SELECT value FROM catalog_metadata WHERE key='schema'")) ||
            !query.next() || query.value(0).toString() != QStringLiteral("sn100-catalog-offline-v1"))
        {
            fail(0, QStringLiteral("initialize"),
                 QStringLiteral("现有数据库不是本模块隔离结构，保留原文件。"));
            release();
            return;
        }
    }
    if (!m_database.transaction())
    {
        fail(0, QStringLiteral("initialize"), m_database.lastError().text());
        release();
        return;
    }
    for (const QString &statement : schema)
    {
        if (!query.exec(statement))
        {
            m_database.rollback();
            fail(0, QStringLiteral("initialize"), sqlError(query));
            release();
            return;
        }
    }
    const QStringList additions = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS project_algorithm(project_id INTEGER PRIMARY KEY REFERENCES project_base(id) ON DELETE CASCADE,version INTEGER NOT NULL CHECK(version=1),coefficient_a REAL NOT NULL,coefficient_b REAL NOT NULL,coefficient_c REAL NOT NULL,auto_apply INTEGER NOT NULL CHECK(auto_apply IN(0,1)))"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS project_advanced(project_id INTEGER PRIMARY KEY REFERENCES project_base(id) ON DELETE CASCADE,version INTEGER NOT NULL CHECK(version=1),payload TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS catalog_project_reference(owner_module TEXT NOT NULL,reference_key TEXT NOT NULL,project_id INTEGER NOT NULL REFERENCES project_base(id) ON DELETE RESTRICT,PRIMARY KEY(owner_module,reference_key))"),
        QStringLiteral("INSERT OR IGNORE INTO catalog_metadata VALUES('project_advanced_schema','sn100-project-advanced-local-v1')"),
        QStringLiteral("INSERT OR IGNORE INTO catalog_metadata VALUES('project_algorithm_schema','sn100-project-algorithm-v1')")};
    if (!query.exec(QStringLiteral("SELECT value FROM catalog_metadata WHERE key='project_algorithm_schema'")) ||
        (query.next() && query.value(0).toString() != QStringLiteral("sn100-project-algorithm-v1")))
    {
        m_database.rollback();
        fail(0, QStringLiteral("initialize"), QStringLiteral("算法配置结构未知或读取失败，保留原文件。"));
        release();
        return;
    }
    if (!query.exec(QStringLiteral("SELECT value FROM catalog_metadata WHERE key='project_advanced_schema'")))
    {
        m_database.rollback();
        fail(0, QStringLiteral("initialize"), sqlError(query));
        release();
        return;
    }
    if (query.next() && query.value(0).toString() != QStringLiteral("sn100-project-advanced-local-v1"))
    {
        m_database.rollback();
        fail(0, QStringLiteral("initialize"), QStringLiteral("高级项目结构版本未知，保留原文件。"));
        release();
        return;
    }
    for (const auto &statement : additions)
    {
        if (!query.exec(statement))
        {
            m_database.rollback();
            fail(0, QStringLiteral("initialize"), sqlError(query));
            release();
            return;
        }
    }
    if (newDatabase && !query.exec(QStringLiteral(
                           "INSERT INTO catalog_metadata VALUES('schema','sn100-catalog-offline-v1')")))
    {
        m_database.rollback();
        fail(0, QStringLiteral("initialize"), sqlError(query));
        release();
        return;
    }
    if (!m_database.commit())
    {
        fail(0, QStringLiteral("initialize"), m_database.lastError().text());
        release();
        return;
    }
    query.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    if (newDatabase && seedDemo)
    {
        if (!m_database.transaction())
        {
            fail(0, QStringLiteral("initialize"), m_database.lastError().text());
            release();
            return;
        }
        bool ok =
            query.exec(QStringLiteral("INSERT INTO option_unit VALUES('s',1),('a.u.',1),('a.u.x10',10)"));
        ProjectRecord demo;
        demo.instrumentProjectId = QStringLiteral("OFFLINE-001");
        demo.name = QStringLiteral("DEMO_A");
        demo.fullName = QStringLiteral("离线项目A");
        demo.fixed = true;
        ProjectResult time;
        time.type = QStringLiteral("Time");
        time.name = QStringLiteral("DEMO_A");
        time.unit = QStringLiteral("s");
        time.mainResult = true;
        ProjectResult amount;
        amount.type = QStringLiteral("Calibration");
        amount.name = QStringLiteral("DEMO_A_C");
        amount.unit = QStringLiteral("a.u.");
        demo.results = {time, amount};
        // Explicitly anonymous labels avoid importing or inventing clinical
        // dilution/volume parameters. They only exercise selection/default rules.
        demo.sampleRates = {{QStringLiteral("OFFLINE_RATE_A"), true, true, 1},
                            {QStringLiteral("OFFLINE_RATE_B"), true, false, 2}};
        QString message;
        qint64 id = 0;
        ok = ok && writeProject(demo, true, &message, &id);
        if (!ok || !m_database.commit())
        {
            m_database.rollback();
            fail(0, QStringLiteral("initialize"), message.isEmpty() ? sqlError(query) : message);
            release();
            return;
        }
    }
    if (!marked)
    {
        QFile marker(dir.filePath(QString::fromLatin1(markerName)));
        if (!marker.open(QIODevice::WriteOnly) || marker.write("sn100-catalog-offline-v1\n") < 0)
        {
            fail(0, QStringLiteral("initialize"), QStringLiteral("无法写入隔离目录标记。"));
            release();
            return;
        }
    }
    m_ready = true;
    if (!m_preparingAuxiliary)
    {
        emit sourceProfileChanged(directory, false);
        emit ready(databasePath);
    }
}
bool ProjectsStore::openOriginal(QString root, QString referenceCatalogDirectory, QString *message)
{
    std::unique_ptr<OriginalProjectRepository> candidate(new OriginalProjectRepository);
    if (!candidate->initialize(root, referenceCatalogDirectory, message)) return false;
    // 尚未移植的页面仍用独立空catalog；此处不向它转换原项目或单位。
    if (!referenceCatalogDirectory.isEmpty() && !m_database.isValid())
    {
        m_preparingAuxiliary = true;
        initialize(referenceCatalogDirectory, false);
        m_preparingAuxiliary = false;
        if (!m_ready)
        {
            *message = QStringLiteral("其他页面依赖的隔离catalog初始化失败，原目录未接入。");
            return false;
        }
    }
    m_original.swap(candidate);
    m_referenceCatalogDirectory = referenceCatalogDirectory;
    m_ready = true;
    return true;
}
void ProjectsStore::initializeOriginal(QString root, QString referenceCatalogDirectory)
{
    if (QThread::currentThread() != thread() || (QCoreApplication::instance() &&
        QThread::currentThread() == QCoreApplication::instance()->thread()))
    { fail(0, QStringLiteral("initializeOriginal"), QStringLiteral("原库初始化须在存储工作线程执行。")); return; }
    QString message;
    if (!openOriginal(root, referenceCatalogDirectory, &message))
    { fail(0, QStringLiteral("initializeOriginal"), message); return; }
    m_lastOriginalWriteRequest = 0;
    emit sourceProfileChanged(m_original->root(), true);
    emit ready(QDir(m_original->root()).filePath(QStringLiteral("db/systemInfo/option_Unit.db")));
}
void ProjectsStore::openOriginalDirectory(qint64 requestId, QString root)
{
    const QString operation = QStringLiteral("openOriginalDirectory");
    if (QThread::currentThread() != thread() || (QCoreApplication::instance() &&
        QThread::currentThread() == QCoreApplication::instance()->thread()) || requestId <= 0 || !canEdit(m_session))
    { fail(requestId, operation, QStringLiteral("切换原配置目录需要允许操作的工程师或管理员身份。")); return; }
    // 其他页面在启动时绑定资料源；不能在运行中把普通catalog换成原项目编号。
    if (!m_original)
    { fail(requestId, operation, QStringLiteral("本次应用使用普通项目库。请关闭应用，使用 --original-db-root 指定原工作目录后重新启动，再选择其他原工作目录。")); return; }
    if (requestId <= m_lastOriginalWriteRequest)
    { fail(requestId, operation, QStringLiteral("重复或迟到的目录切换请求未执行。")); return; }
    const QString auxiliary = m_referenceCatalogDirectory.isEmpty() && m_database.isValid()
        ? QFileInfo(m_database.databaseName()).absolutePath() : m_referenceCatalogDirectory;
    m_lastOriginalWriteRequest = requestId;
    QString message;
    if (!openOriginal(root, auxiliary, &message)) { fail(requestId, operation, message); return; }
    emit sourceProfileChanged(m_original->root(), true);
    emit ready(QDir(m_original->root()).filePath(QStringLiteral("db/systemInfo/option_Unit.db")));
    emit succeeded(requestId, operation, 0);
}
bool ProjectsStore::checkOriginalRequest(qint64 requestId, const QString &operation, bool write)
{
    if (!check(requestId, operation, write)) return false;
    if (!m_original || requestId <= 0)
    { fail(requestId, operation, QStringLiteral("原库操作需要已接入的原工作目录和有效请求号。")); return false; }
    if (write)
    {
        if (requestId <= m_lastOriginalWriteRequest)
        { fail(requestId, operation, QStringLiteral("重复或迟到的原库写入请求未执行，请重新读取后操作。")); return false; }
        m_lastOriginalWriteRequest = requestId;
    }
    return true;
}
void ProjectsStore::importOriginalFile(qint64 requestId, QString filePath)
{
    const QString operation = QStringLiteral("importOriginalFile");
    if (!checkOriginalRequest(requestId, operation, true)) return;
    qint64 id = 0;
    QString message;
    if (!m_original->importFile(filePath, &id, &message)) { fail(requestId, operation, message); return; }
    emit succeeded(requestId, operation, id);
}
void ProjectsStore::exportOriginalBundle(qint64 requestId, qint64 projectId, QString targetDirectory)
{
    const QString operation = QStringLiteral("exportOriginalBundle");
    if (!checkOriginalRequest(requestId, operation, true)) return;
    QString message;
    if (!m_original->exportBundle(projectId, targetDirectory, &message)) { fail(requestId, operation, message); return; }
    emit succeeded(requestId, operation, projectId);
}
void ProjectsStore::setSession(AccountSession session)
{
    if (QThread::currentThread() == thread())
    {
        m_session = session;
    }
}
bool ProjectsStore::read(ProjectRecords *projects, ProjectUnits *units, QString *message)
{
    if (m_original)
    {
        QStringList reagents, cleaners, diluents;
        return m_original->load(projects, units, &reagents, &cleaners, &diluents, message);
    }
    projects->clear();
    units->clear();
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("SELECT value FROM catalog_metadata WHERE key='current_format_schema'")))
    { *message = sqlError(query); return false; }
    const bool selectionMarked = query.next();
    if (query.lastError().isValid()) { *message = sqlError(query); return false; }
    if (selectionMarked && query.value(0).toString() != QStringLiteral("sn100-current-result-format-source-v1"))
    { *message = QStringLiteral("结果选择配置版本未知，保留原数据。"); return false; }
    query.finish();
    const bool haveSelections = m_database.tables().contains(QStringLiteral("project_result_format_source"));
    if (haveSelections != selectionMarked)
    { *message = QStringLiteral("结果选择表和版本标记不一致，未读取或迁移未知配置。"); return false; }
    if (!query.exec(QStringLiteral("SELECT unit,ratio FROM option_unit ORDER BY length(unit),unit")))
    {
        *message = sqlError(query);
        return false;
    }
    while (query.next())
    {
        units->append({query.value(0).toString(), query.value(1).toDouble()});
    }
    if (query.lastError().isValid()) { *message = sqlError(query); return false; }
    if (!query.exec(QStringLiteral(
            "SELECT id,project_id,project_en_name,project_cn_name,project_lis_code,is_used,fixed,change_time "
            "FROM project_base ORDER BY id")))
    {
        *message = sqlError(query);
        return false;
    }
    while (query.next())
    {
        ProjectRecord p;
        p.id = query.value(0).toLongLong();
        p.instrumentProjectId = query.value(1).toString();
        p.name = query.value(2).toString();
        p.fullName = query.value(3).toString();
        p.lisCode = query.value(4).toString();
        p.enabled = query.value(5).toBool();
        p.fixed = query.value(6).toBool();
        p.changedAt = query.value(7).toString();
        QSqlQuery results(m_database);
        results.prepare(QStringLiteral(
            "SELECT "
            "result_type,result_name,decimal_point,test_low_range,test_up_range,normal_low_range,normal_up_"
            "range,lis_code,unit_name,main_result FROM project_result WHERE project_id=? ORDER BY rowid"));
        results.addBindValue(p.id);
        if (!results.exec())
        {
            *message = sqlError(results);
            return false;
        }
        while (results.next())
        {
            ProjectResult r;
            r.type = results.value(0).toString();
            r.name = results.value(1).toString();
            r.decimals = results.value(2).toInt();
            r.testLow = results.value(3).toDouble();
            r.testHigh = results.value(4).toDouble();
            r.normalLow = results.value(5).toDouble();
            r.normalHigh = results.value(6).toDouble();
            r.lisCode = results.value(7).toString();
            r.unit = results.value(8).toString();
            r.mainResult = results.value(9).toBool();
            p.results.append(r);
        }
        if (results.lastError().isValid()) { *message = sqlError(results); return false; }
        if (haveSelections)
        {
            results.prepare(QStringLiteral("SELECT result_type,result_id,is_used,display,typeof(result_id),typeof(is_used),typeof(display) FROM project_result_format_source WHERE project_id=?"));
            results.addBindValue(p.id);
            if (!results.exec()) { *message = sqlError(results); return false; }
            while (results.next())
            {
                const QString type = results.value(0).toString();
                auto result = std::find_if(p.results.begin(), p.results.end(),
                    [&](const ProjectResult &candidate) { return candidate.type == type; });
                // 原选择字段不合法时仍保留数据库；格式读取会将它标为未知。
                if (result != p.results.end() && resultSlot(type) > 0 && results.value(1).toLongLong() == resultSlot(type) &&
                    results.value(4).toString() == QStringLiteral("integer") && results.value(5).toString() == QStringLiteral("integer") &&
                    results.value(6).toString() == QStringLiteral("integer") &&
                    (results.value(2).toLongLong() == 0 || results.value(2).toLongLong() == 1) &&
                    (results.value(3).toLongLong() == 0 || results.value(3).toLongLong() == 1))
                {
                    result->selectionKnown = true;
                    result->sourceResultId = resultSlot(type);
                    result->used = results.value(2).toBool();
                    result->display = results.value(3).toBool();
                }
            }
            if (results.lastError().isValid()) { *message = sqlError(results); return false; }
        }
        results.prepare(QStringLiteral("SELECT version,coefficient_a,coefficient_b,coefficient_c,auto_apply FROM project_algorithm WHERE project_id=?"));
        results.addBindValue(p.id);
        if (!results.exec()) { *message = sqlError(results); return false; }
        if (results.next())
        {
            bool a = false, b = false, c = false;
            p.algorithm.configured = true;
            p.algorithm.coefficientA = results.value(1).toDouble(&a);
            p.algorithm.coefficientB = results.value(2).toDouble(&b);
            p.algorithm.coefficientC = results.value(3).toDouble(&c);
            p.algorithm.autoApply = results.value(4).toBool();
            if (results.value(0).toInt() != 1 || !a || !b || !c ||
                (results.value(4).toInt() != 0 && results.value(4).toInt() != 1) || !validateAlgorithm(p.algorithm).isEmpty())
            { *message = QStringLiteral("算法配置损坏或版本未知，保留原数据。项目ID=%1").arg(p.id); return false; }
        }
        if (results.lastError().isValid()) { *message = sqlError(results); return false; }
        results.prepare(QStringLiteral("SELECT sample_rate,sample_option,sample_default_option,xuhao FROM "
                                       "project_sample_rate WHERE project_id=? ORDER BY xuhao"));
        results.addBindValue(p.id);
        if (!results.exec())
        {
            *message = sqlError(results);
            return false;
        }
        while (results.next())
        {
            p.sampleRates.append({results.value(0).toString(), results.value(1).toBool(),
                                  results.value(2).toBool(), results.value(3).toInt()});
        }
        if (results.lastError().isValid()) { *message = sqlError(results); return false; }
        results.prepare(QStringLiteral("SELECT version,payload FROM project_advanced WHERE project_id=?"));
        results.addBindValue(p.id);
        if (!results.exec())
        {
            *message = sqlError(results);
            return false;
        }
        if (results.next() && (results.value(0).toInt() != 1 ||
            !parseAdvanced(results.value(1).toString(), &p.advanced)))
        {
            *message = QStringLiteral("高级方案损坏或版本不支持，保留原数据。项目ID=%1").arg(p.id);
            return false;
        }
        QStringList labels;
        if (results.lastError().isValid()) { *message = sqlError(results); return false; }
        for (const auto &rate : p.sampleRates) labels.append(rate.sampleRate);
        const QString invalid = ProjectAdvancedEditor::validate(p.advanced, labels);
        if (!invalid.isEmpty())
        {
            *message = QStringLiteral("已存高级方案未通过校验，保留原数据。项目ID=%1：%2").arg(p.id).arg(invalid);
            return false;
        }
        projects->append(p);
    }
    if (query.lastError().isValid()) { *message = sqlError(query); return false; }
    return true;
}
void ProjectsStore::load(qint64 requestId)
{
    if (!check(requestId, QStringLiteral("load")))
    {
        return;
    }
    ProjectRecords projects;
    ProjectUnits units;
    QString message;
    QStringList reagentNames, cleanerNames, diluentNames;
    if (m_original)
    {
        if (!m_original->load(&projects, &units, &reagentNames, &cleanerNames, &diluentNames, &message))
        { fail(requestId, QStringLiteral("load"), message); return; }
        emit reagentOptionsReady(reagentNames, cleanerNames);
        emit diluentOptionsReady(diluentNames);
        emit snapshotReady(requestId, projects, units);
        return;
    }
    if (!m_database.transaction())
    {
        fail(requestId, QStringLiteral("load"), m_database.lastError().text());
        return;
    }
    bool ok = read(&projects, &units, &message);
    if (ok && m_database.tables().contains(QStringLiteral("reagent_list")))
    {
        QSqlQuery query(m_database);
        ok = query.exec(QStringLiteral("SELECT reagent_name,reagent_type FROM reagent_list WHERE valid=1 ORDER BY id"));
        if (!ok) message = sqlError(query);
        while (ok && query.next())
        {
            if (query.value(1).toString() == QStringLiteral("试剂")) reagentNames.append(query.value(0).toString());
            if (query.value(1).toString() == QStringLiteral("清洗液")) cleanerNames.append(query.value(0).toString());
            if (query.value(1).toString() == QStringLiteral("缓冲稀释液")) diluentNames.append(query.value(0).toString());
        }
        if (ok && query.lastError().isValid()) { ok = false; message = sqlError(query); }
    }
    if (!ok || !m_database.commit())
    {
        if (message.isEmpty()) message = m_database.lastError().text();
        rollbackWrite(requestId, QStringLiteral("load"), message);
        return;
    }
    emit reagentOptionsReady(reagentNames, cleanerNames);
    emit diluentOptionsReady(diluentNames);
    emit snapshotReady(requestId, projects, units);
}
bool ProjectsStore::writeProject(ProjectRecord p, bool insert, QString *message, qint64 *id)
{
    QSqlQuery query(m_database);
    query.prepare(insert ? QStringLiteral("INSERT INTO "
                                          "project_base(project_id,project_en_name,project_cn_name,project_"
                                          "lis_code,is_used,fixed,change_time) VALUES(?,?,?,?,?,?,?)")
                         : QStringLiteral("UPDATE project_base SET "
                                          "project_id=?,project_en_name=?,project_cn_name=?,project_lis_code="
                                          "?,is_used=?,fixed=?,change_time=? WHERE id=?"));
    query.addBindValue(p.instrumentProjectId);
    query.addBindValue(p.name);
    query.addBindValue(p.fullName);
    query.addBindValue(p.lisCode.isNull() ? QStringLiteral("") : p.lisCode);
    query.addBindValue(p.enabled ? 1 : 0);
    query.addBindValue(p.fixed ? 1 : 0);
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (!insert)
    {
        query.addBindValue(p.id);
    }
    if (!query.exec() || (!insert && query.numRowsAffected() != 1))
    {
        *message = sqlError(query);
        return false;
    }
    *id = insert ? query.lastInsertId().toLongLong() : p.id;
    query.prepare(QStringLiteral("DELETE FROM project_result WHERE project_id=?"));
    query.addBindValue(*id);
    if (!query.exec())
    {
        *message = sqlError(query);
        return false;
    }
    for (const ProjectResult &r : p.results)
    {
        query.prepare(QStringLiteral("INSERT INTO project_result VALUES(?,?,?,?,?,?,?,?,?,?,?)"));
        query.addBindValue(*id);
        query.addBindValue(r.type);
        query.addBindValue(r.name);
        query.addBindValue(r.decimals);
        query.addBindValue(r.testLow);
        query.addBindValue(r.testHigh);
        query.addBindValue(r.normalLow);
        query.addBindValue(r.normalHigh);
        query.addBindValue(r.lisCode.isNull() ? QStringLiteral("") : r.lisCode);
        query.addBindValue(r.unit);
        query.addBindValue(r.mainResult ? 1 : 0);
        if (!query.exec())
        {
            *message = sqlError(query);
            return false;
        }
    }
    query.prepare(QStringLiteral("DELETE FROM project_sample_rate WHERE project_id=?"));
    query.addBindValue(*id);
    if (!query.exec())
    {
        *message = sqlError(query);
        return false;
    }
    for (const ProjectSampleRate &rate : p.sampleRates)
    {
        query.prepare(QStringLiteral("INSERT INTO project_sample_rate VALUES(?,?,?,?,?)"));
        query.addBindValue(*id);
        query.addBindValue(rate.sampleRate);
        query.addBindValue(rate.sampleOption ? 1 : 0);
        query.addBindValue(rate.defaultOption ? 1 : 0);
        query.addBindValue(rate.order);
        if (!query.exec())
        {
            *message = sqlError(query);
            return false;
        }
    }
    query.prepare(QStringLiteral("DELETE FROM project_advanced WHERE project_id=?"));
    query.addBindValue(*id);
    if (!query.exec())
    {
        *message = sqlError(query);
        return false;
    }
    if (p.advanced.configured)
    {
        query.prepare(QStringLiteral("INSERT INTO project_advanced VALUES(?,1,?)"));
        query.addBindValue(*id);
        query.addBindValue(advancedJson(p.advanced));
        if (!query.exec())
        {
            *message = sqlError(query);
            return false;
        }
    }
    query.prepare(QStringLiteral("DELETE FROM project_algorithm WHERE project_id=?"));
    query.addBindValue(*id);
    if (!query.exec()) { *message = sqlError(query); return false; }
    if (p.algorithm.configured)
    {
        query.prepare(QStringLiteral("INSERT INTO project_algorithm VALUES(?,1,?,?,?,?)"));
        query.addBindValue(*id);
        query.addBindValue(p.algorithm.coefficientA);
        query.addBindValue(p.algorithm.coefficientB);
        query.addBindValue(p.algorithm.coefficientC);
        query.addBindValue(p.algorithm.autoApply ? 1 : 0);
        if (!query.exec()) { *message = sqlError(query); return false; }
    }
    bool knownSelection = false;
    for (const auto &result : p.results) knownSelection = knownSelection || result.selectionKnown;
    if (knownSelection)
    {
        if (!ensureResultSelections(m_database, message)) return false;
        for (const auto &result : p.results)
        {
            if (!result.selectionKnown) continue;
            query.prepare(QStringLiteral("INSERT OR REPLACE INTO project_result_format_source(project_id,result_type,result_id,is_used,display) VALUES(?,?,?,?,?)"));
            query.addBindValue(*id);
            query.addBindValue(result.type);
            query.addBindValue(result.sourceResultId);
            query.addBindValue(result.used ? 1 : 0);
            query.addBindValue(result.display ? 1 : 0);
            if (!query.exec()) { *message = sqlError(query); return false; }
        }
    }
    return writeActionReferences(m_database, *id, p.advanced, message);
}
bool ProjectsStore::beginWrite(qint64 requestId, const QString &operation)
{
    if (!check(requestId, operation, true)) return false;
    QSqlQuery query(m_database);
    if (query.exec(QStringLiteral("BEGIN IMMEDIATE"))) return true;
    fail(requestId, operation, sqlError(query));
    return false;
}
void ProjectsStore::rollbackWrite(qint64 requestId, const QString &operation, QString message)
{
    if (!m_database.rollback())
    {
        message += QStringLiteral("；回滚结果未经确认：") + m_database.lastError().text();
        m_ready = false;
        m_database.close();
        message += QStringLiteral("；已停止读写，请重新打开资料库核查。不能推定已保存或已撤销。");
    }
    fail(requestId, operation, message);
}
void ProjectsStore::create(qint64 requestId, ProjectRecord project)
{
    if (m_original)
    {
        if (!checkOriginalRequest(requestId, QStringLiteral("create"), true)) return;
        fail(requestId, QStringLiteral("create"), QStringLiteral("原项目应从完整原DB导入或复制，避免生成缺少原算法参数的项目。"));
        return;
    }
    const QString operation = QStringLiteral("create");
    if (!beginWrite(requestId, operation)) return;
    ProjectRecords projects;
    ProjectUnits units;
    QString message;
    if (!read(&projects, &units, &message)) { rollbackWrite(requestId, operation, message); return; }
    if (project.id != 0 || project.fixed)
    { rollbackWrite(requestId, operation, QStringLiteral("新增项目不能指定已有本地编号或设置为固定项目。")); return; }
    int resultCount = project.results.size();
    for (const auto &existing : projects) resultCount += existing.results.size();
    if (projects.size() >= 1024 || resultCount > 4096)
    { rollbackWrite(requestId, operation, QStringLiteral("新增后将超过1024项目或4096结果的本地配置容量。")); return; }
    message = validate(project, units);
    for (const auto &existing : projects)
        if (existing.name.compare(project.name, Qt::CaseInsensitive) == 0)
            message = QStringLiteral("同名项目已存在，请更换名称；没有覆盖原项目。");
    if (!message.isEmpty()) { rollbackWrite(requestId, operation, message); return; }
    qint64 id = 0;
    if (!writeProject(project, true, &message, &id) || !m_database.commit())
    { rollbackWrite(requestId, operation, message.isEmpty() ? m_database.lastError().text() : message); return; }
    emit succeeded(requestId, operation, id);
}
void ProjectsStore::importFile(qint64 requestId, QString filePath)
{
    if (m_original)
    {
        fail(requestId, QStringLiteral("importFile"), QStringLiteral("当前使用原库，请选择原项目DB导入入口；不将JSON转换为另一套原库。"));
        return;
    }
    const QString operation = QStringLiteral("importFile");
    if (!check(requestId, operation, true)) return;
    const QFileInfo source(filePath);
    if (!source.isAbsolute() || !source.isFile() || source.isSymLink() ||
        source.absoluteFilePath().contains(QStringLiteral("sn100_host_reference"), Qt::CaseInsensitive))
    { fail(requestId, operation, QStringLiteral("请选择明确的JSON配置文件绝对路径，禁止参考工程或符号链接。")); return; }
    for (QFileInfo current(source.absoluteFilePath()); ; current.setFile(current.absolutePath()))
    {
        if (current.isSymLink()) { fail(requestId, operation, QStringLiteral("导入路径不能经过符号链接。")); return; }
        if (current.absoluteFilePath() == current.absolutePath()) break;
    }
    QFile file(source.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly) || file.size() < 1 || file.size() > 2 * 1024 * 1024)
    { fail(requestId, operation, QStringLiteral("无法读取文件，或文件为空、超过2MiB。")); return; }
    const QByteArray bytes = file.read(2 * 1024 * 1024 + 1);
    if (file.error() != QFileDevice::NoError || !file.atEnd() || bytes.size() > 2 * 1024 * 1024)
    { fail(requestId, operation, QStringLiteral("配置文件读取失败或读取时大小改变，未导入。")); return; }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
    { fail(requestId, operation, QStringLiteral("JSON格式错误：%1；位置%2。").arg(error.errorString()).arg(error.offset)); return; }
    const auto root = document.object();
    if (!exactKeys(root, {QStringLiteral("format"), QStringLiteral("units"), QStringLiteral("projects")}) ||
        root.value(QStringLiteral("format")).toString() != QStringLiteral("sn100-project-config-v1") ||
        !root.value(QStringLiteral("units")).isArray() || !root.value(QStringLiteral("projects")).isArray())
    { fail(requestId, operation, QStringLiteral("仅接受sn100-project-config-v1完整配置，不能导入旧数据库或未知字段。")); return; }
    const auto unitInputs = root.value(QStringLiteral("units")).toArray();
    const auto projectInputs = root.value(QStringLiteral("projects")).toArray();
    if (unitInputs.size() > 256 || projectInputs.size() > 1024 || (unitInputs.isEmpty() && projectInputs.isEmpty()))
    { fail(requestId, operation, QStringLiteral("配置为空或超过256单位、1024项目的本地容量。")); return; }
    ProjectUnits incomingUnits;
    ProjectRecords incomingProjects;
    QString message;
    for (const auto &item : unitInputs)
    {
        if (!item.isObject() || !exactKeys(item.toObject(), {QStringLiteral("name"), QStringLiteral("ratio")}) ||
            !item.toObject().value(QStringLiteral("name")).isString() || !item.toObject().value(QStringLiteral("ratio")).isDouble())
        { fail(requestId, operation, QStringLiteral("单位须完整提供name字符串和ratio数值，没有忽略未知字段。")); return; }
        const ProjectUnit unit{item.toObject().value(QStringLiteral("name")).toString(), item.toObject().value(QStringLiteral("ratio")).toDouble()};
        message = validateUnit(unit);
        if (!message.isEmpty()) { fail(requestId, operation, message); return; }
        incomingUnits.append(unit);
    }
    for (int row = 0; row < projectInputs.size(); ++row)
    {
        ProjectRecord project;
        if (!projectInputs[row].isObject() || !jsonProject(projectInputs[row].toObject(), &project, &message))
        { fail(requestId, operation, QStringLiteral("导入项目第%1项：%2").arg(row + 1).arg(message.isEmpty() ? QStringLiteral("必须是对象。") : message)); return; }
        incomingProjects.append(project);
    }
    if (!beginWrite(requestId, operation)) return;
    ProjectRecords existing;
    ProjectUnits units;
    if (!read(&existing, &units, &message)) { rollbackWrite(requestId, operation, message); return; }
    int resultCount = 0;
    for (const auto &project : existing) resultCount += project.results.size();
    for (const auto &project : incomingProjects) resultCount += project.results.size();
    if (units.size() + incomingUnits.size() > 256 || existing.size() + incomingProjects.size() > 1024 || resultCount > 4096)
    { rollbackWrite(requestId, operation, QStringLiteral("导入后将超过本地配置容量，未截断输入。")); return; }
    QSet<QString> unitNames;
    for (const auto &unit : units) unitNames.insert(unit.name);
    for (const auto &unit : incomingUnits)
    {
        if (unitNames.contains(unit.name))
        { rollbackWrite(requestId, operation, QStringLiteral("单位“%1”重复或已存在；复用已有单位时请从导入units中移除该项，不会覆盖。").arg(unit.name)); return; }
        unitNames.insert(unit.name);
        if (!writeUnit(unit, &message)) { rollbackWrite(requestId, operation, message); return; }
        units.append(unit);
    }
    QStringList projectNames;
    for (const auto &project : existing) projectNames.append(project.name);
    qint64 lastId = 0;
    for (const auto &project : incomingProjects)
    {
        if (projectNames.contains(project.name, Qt::CaseInsensitive))
        { rollbackWrite(requestId, operation, QStringLiteral("项目“%1”重复或已存在，整批未覆盖。").arg(project.name)); return; }
        projectNames.append(project.name);
        message = validate(project, units);
        if (!message.isEmpty() || !writeProject(project, true, &message, &lastId))
        { rollbackWrite(requestId, operation, QStringLiteral("项目“%1”：%2").arg(project.name, message)); return; }
    }
    if (!m_database.commit()) { rollbackWrite(requestId, operation, m_database.lastError().text()); return; }
    emit succeeded(requestId, operation, lastId);
}
void ProjectsStore::save(qint64 requestId, ProjectRecord project)
{
    if (m_original)
    {
        const QString operation = QStringLiteral("save");
        if (!checkOriginalRequest(requestId, operation, true)) return;
        if (project.originalRoot != m_original->root())
        { fail(requestId, operation, QStringLiteral("草稿所属原目录已变化，未把旧输入写入新目录，请重新读取。")); return; }
        QString message;
        qint64 id = 0;
        bool changed = false;
        if (!m_original->save(project, &id, &changed, &message)) { fail(requestId, operation, message); return; }
        emit succeeded(requestId, operation, id);
        return;
    }
    // 身份、单位和引用在同一次写事务中检查，避免另一写者在核对后改动。
    if (!beginWrite(requestId, QStringLiteral("save"))) return;
    ProjectRecords projects;
    ProjectUnits units;
    QString message;
    if (!read(&projects, &units, &message))
    {
        rollbackWrite(requestId, QStringLiteral("save"), message);
        return;
    }
    const auto found = std::find_if(projects.cbegin(), projects.cend(),
                                    [&project](const ProjectRecord &p) { return p.id == project.id; });
    if (found == projects.cend())
    {
        rollbackWrite(requestId, QStringLiteral("save"), QStringLiteral("待保存项目已不存在，请刷新。"));
        return;
    }
    // 已有项目的仪器编号和固定标记不能通过普通编辑改动。
    project.instrumentProjectId = found->instrumentProjectId;
    project.fixed = found->fixed;
    // 稀释选项和加样行属于同一份草稿，必须一起校验并保存，不能用旧值覆盖。
    if (project.results.size() != found->results.size())
    {
        rollbackWrite(requestId, QStringLiteral("save"), QStringLiteral("增删结果类型请使用完整结果配置入口。"));
        return;
    }
    for (int i = 0; i < project.results.size(); ++i)
    {
        if (project.results[i].type != found->results[i].type)
        {
            rollbackWrite(requestId, QStringLiteral("save"), QStringLiteral("修改结果类型请使用完整结果配置入口。"));
            return;
        }
        if (project.results[i].type != QStringLiteral("Calibration") &&
            project.results[i].type != QStringLiteral("Rate"))
        {
            project.results[i].unit = found->results[i].unit;
        }
        if (found->results[i].selectionKnown && !project.results[i].selectionKnown)
        {
            rollbackWrite(requestId, QStringLiteral("save"), QStringLiteral("结果选择已由其他操作明确配置，请刷新，不能用未知旧草稿覆盖。"));
            return;
        }
    }
    if (found->algorithm.configured && !project.algorithm.configured)
    { rollbackWrite(requestId, QStringLiteral("save"), QStringLiteral("算法参数已经配置，请刷新，不能用未知旧草稿清除。")); return; }
    for (const auto &existing : projects)
        if (existing.id != project.id && existing.name.compare(project.name, Qt::CaseInsensitive) == 0)
        { rollbackWrite(requestId, QStringLiteral("save"), QStringLiteral("同名项目已存在，不能覆盖。")); return; }
    message = validate(project, units);
    if (!message.isEmpty())
    {
        rollbackWrite(requestId, QStringLiteral("save"), message);
        return;
    }
    qint64 id = 0;
    if (project.name != found->name || project.fullName != found->fullName)
    {
        QSqlQuery reference(m_database);
        reference.prepare(QStringLiteral("SELECT owner_module,reference_key FROM catalog_project_reference WHERE project_id=? LIMIT 1"));
        reference.addBindValue(project.id);
        if (!reference.exec())
        {
            rollbackWrite(requestId, QStringLiteral("save"), QStringLiteral("项目引用检查失败，不能假定未被使用。%1").arg(sqlError(reference)));
            return;
        }
        const bool referenced = reference.next();
        if (referenced || reference.lastError().isValid())
        { rollbackWrite(requestId, QStringLiteral("save"), QStringLiteral("项目被外部记录引用或引用检查失败，不能改名；请先在所属模块解除引用。")); return; }
    }
    if (!writeProject(project, false, &message, &id) || !m_database.commit())
    {
        rollbackWrite(requestId, QStringLiteral("save"), message.isEmpty() ? m_database.lastError().text() : message);
        return;
    }
    emit succeeded(requestId, QStringLiteral("save"), id);
}
void ProjectsStore::copy(qint64 requestId, qint64 sourceId, QString newName)
{
    if (m_original)
    {
        const QString operation = QStringLiteral("copy");
        if (!checkOriginalRequest(requestId, operation, true)) return;
        qint64 id = 0;
        QString message;
        if (!m_original->copy(sourceId, newName, &id, &message)) { fail(requestId, operation, message); return; }
        emit succeeded(requestId, operation, id);
        return;
    }
    if (!beginWrite(requestId, QStringLiteral("copy")))
    {
        return;
    }
    ProjectRecords projects;
    ProjectUnits units;
    QString message;
    if (!read(&projects, &units, &message))
    {
        rollbackWrite(requestId, QStringLiteral("copy"), message);
        return;
    }
    const auto found = std::find_if(projects.cbegin(), projects.cend(),
                                    [sourceId](const ProjectRecord &p) { return p.id == sourceId; });
    if (found == projects.cend())
    {
        rollbackWrite(requestId, QStringLiteral("copy"), QStringLiteral("源项目已不存在。"));
        return;
    }
    ProjectRecord project = *found;
    project.id = 0;
    project.name = newName;
    project.fixed = false;
    int resultCount = project.results.size();
    for (const auto &existing : projects) resultCount += existing.results.size();
    if (projects.size() >= 1024 || resultCount > 4096)
    { rollbackWrite(requestId, QStringLiteral("copy"), QStringLiteral("复制后将超过1024项目或4096结果的本地配置容量。")); return; }
    for (const auto &existing : projects)
        if (existing.name.compare(newName, Qt::CaseInsensitive) == 0)
        { rollbackWrite(requestId, QStringLiteral("copy"), QStringLiteral("复制名称已存在，请更换名称。")); return; }
    for (ProjectResult &r : project.results)
    {
        r.name.replace(found->name, newName);
    }
    message = validate(project, units);
    if (!message.isEmpty())
    {
        rollbackWrite(requestId, QStringLiteral("copy"), message);
        return;
    }
    qint64 id = 0;
    if (!writeProject(project, true, &message, &id) || !m_database.commit())
    {
        rollbackWrite(requestId, QStringLiteral("copy"), message.isEmpty() ? m_database.lastError().text() : message);
        return;
    }
    emit succeeded(requestId, QStringLiteral("copy"), id);
}
void ProjectsStore::remove(qint64 requestId, qint64 projectId)
{
    if (m_original)
    {
        const QString operation = QStringLiteral("remove");
        if (!checkOriginalRequest(requestId, operation, true)) return;
        QString message;
        if (!m_original->remove(projectId, &message)) { fail(requestId, operation, message); return; }
        emit succeeded(requestId, operation, projectId);
        return;
    }
    if (!beginWrite(requestId, QStringLiteral("remove")))
    {
        return;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM project_base WHERE id=? AND fixed=0"));
    query.addBindValue(projectId);
    if (!query.exec())
    {
        rollbackWrite(requestId, QStringLiteral("remove"),
             QStringLiteral("删除失败；项目可能被试剂/样本引用。%1").arg(sqlError(query)));
        return;
    }
    if (query.numRowsAffected() != 1)
    {
        rollbackWrite(requestId, QStringLiteral("remove"), QStringLiteral("固定项目不能删除，或项目已不存在。"));
        return;
    }
    if (!m_database.commit())
    { rollbackWrite(requestId, QStringLiteral("remove"), m_database.lastError().text()); return; }
    emit succeeded(requestId, QStringLiteral("remove"), projectId);
}
bool ProjectsStore::writeUnit(const ProjectUnit &unit, QString *message)
{
    *message = validateUnit(unit);
    if (!message->isEmpty()) return false;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("INSERT INTO option_unit(unit,ratio) VALUES(?,?)"));
    query.addBindValue(unit.name);
    query.addBindValue(unit.ratio);
    if (!query.exec())
    { *message = QStringLiteral("新增单位失败，可能已有同名单位；未覆盖。%1").arg(sqlError(query)); return false; }
    return true;
}
bool ProjectsStore::unitReferenced(const QString &name, bool *referenced, QString *message)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT 1 FROM project_result WHERE unit_name=? LIMIT 1"));
    query.addBindValue(name);
    if (!query.exec()) { *message = sqlError(query); return false; }
    *referenced = query.next();
    if (query.lastError().isValid()) { *message = sqlError(query); return false; }
    return true;
}
void ProjectsStore::addUnit(qint64 requestId, ProjectUnit unit)
{
    if (m_original)
    {
        const QString operation = QStringLiteral("addUnit");
        if (!checkOriginalRequest(requestId, operation, true)) return;
        QString message;
        if (!m_original->addUnit(unit, &message)) { fail(requestId, operation, message); return; }
        emit succeeded(requestId, operation, 0);
        return;
    }
    const QString operation = QStringLiteral("addUnit");
    if (!beginWrite(requestId, operation)) return;
    QString message;
    QSqlQuery count(m_database);
    if (!count.exec(QStringLiteral("SELECT COUNT(*) FROM option_unit")) || !count.next())
    { rollbackWrite(requestId, operation, sqlError(count)); return; }
    if (count.value(0).toLongLong() >= 256)
    { count.finish(); rollbackWrite(requestId, operation, QStringLiteral("单位数量已到256条的本地配置容量。")); return; }
    count.finish();
    if (!writeUnit(unit, &message) || !m_database.commit())
    { rollbackWrite(requestId, operation, message.isEmpty() ? m_database.lastError().text() : message); return; }
    emit succeeded(requestId, operation, 0);
}
void ProjectsStore::updateUnit(qint64 requestId, QString originalName, ProjectUnit unit)
{
    if (m_original)
    {
        const QString operation = QStringLiteral("updateUnit");
        if (!checkOriginalRequest(requestId, operation, true)) return;
        QString message;
        if (!m_original->updateUnit(originalName, unit, &message)) { fail(requestId, operation, message); return; }
        emit succeeded(requestId, operation, 0);
        return;
    }
    const QString operation = QStringLiteral("updateUnit");
    if (!beginWrite(requestId, operation)) return;
    QString message = validateUnit(unit);
    if (!message.isEmpty()) { rollbackWrite(requestId, operation, message); return; }
    bool referenced = false;
    if (!unitReferenced(originalName, &referenced, &message)) { rollbackWrite(requestId, operation, message); return; }
    if (referenced)
    { rollbackWrite(requestId, operation, QStringLiteral("单位已被项目结果使用，不能改名或改变倍率；请新增其他单位。")); return; }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE option_unit SET unit=?,ratio=? WHERE unit=?"));
    query.addBindValue(unit.name);
    query.addBindValue(unit.ratio);
    query.addBindValue(originalName);
    if (!query.exec() || query.numRowsAffected() != 1)
    { rollbackWrite(requestId, operation, QStringLiteral("修改单位失败：原单位已不存在或新名称已被占用。%1").arg(sqlError(query))); return; }
    if (!m_database.commit()) { rollbackWrite(requestId, operation, m_database.lastError().text()); return; }
    emit succeeded(requestId, operation, 0);
}
void ProjectsStore::removeUnit(qint64 requestId, QString name)
{
    if (m_original)
    {
        const QString operation = QStringLiteral("removeUnit");
        if (!checkOriginalRequest(requestId, operation, true)) return;
        fail(requestId, operation, QStringLiteral("原单位删除需要读入时的单位库指纹，请重新读取后使用原单位删除入口。"));
        return;
    }
    const QString operation = QStringLiteral("removeUnit");
    if (!beginWrite(requestId, operation)) return;
    QString message;
    bool referenced = false;
    if (!unitReferenced(name, &referenced, &message)) { rollbackWrite(requestId, operation, message); return; }
    if (referenced) { rollbackWrite(requestId, operation, QStringLiteral("单位已被项目结果使用，不能删除。")); return; }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM option_unit WHERE unit=?"));
    query.addBindValue(name);
    if (!query.exec() || query.numRowsAffected() != 1)
    { rollbackWrite(requestId, operation, QStringLiteral("单位已不存在或删除失败。%1").arg(sqlError(query))); return; }
    if (!m_database.commit()) { rollbackWrite(requestId, operation, m_database.lastError().text()); return; }
    emit succeeded(requestId, operation, 0);
}
void ProjectsStore::removeOriginalUnit(qint64 requestId, QString name, QString expectedFingerprint)
{
    const QString operation = QStringLiteral("removeOriginalUnit");
    if (!checkOriginalRequest(requestId, operation, true)) return;
    QString message;
    if (!m_original->removeUnit(name, expectedFingerprint, &message))
    { fail(requestId, operation, message); return; }
    emit succeeded(requestId, operation, 0);
}
void ProjectsStore::release()
{
    m_ready = false;
    m_original.reset();
    m_referenceCatalogDirectory.clear();
    m_lastOriginalWriteRequest = 0;
    if (m_database.isValid())
    {
        m_database.close();
    }
    m_database = QSqlDatabase();
    if (!m_connectionName.isEmpty())
    {
        QSqlDatabase::removeDatabase(m_connectionName);
    }
    m_connectionName.clear();
}
void ProjectsStore::holdProjectReference(qint64 requestId, QString ownerModule, QString referenceKey,
                                         qint64 projectId)
{
    const QString operation = QStringLiteral("holdProjectReference");
    if (m_original)
    {
        emit projectReferenceError(requestId, operation, QStringLiteral("本批原库仅接项目配置；未接原库的业务不能把原项目编号登记到独立catalog。"));
        return;
    }
    if (QThread::currentThread() != thread() || !m_ready || !m_session.authenticated ||
        !m_session.actionReady || m_session.role < 1 || m_session.role > 5)
    {
        emit projectReferenceError(requestId, operation, QStringLiteral("引用登记需要存储线程、已就绪目录及允许业务操作的登录身份。"));
        return;
    }
    static const QRegularExpression module(QStringLiteral("^[a-z][a-z0-9_]{0,31}\\z"));
    if (!module.match(ownerModule).hasMatch() || referenceKey.isEmpty() || referenceKey.size() > 128 ||
        referenceKey != referenceKey.trimmed() || referenceKey.contains(QChar::Null) || projectId <= 0)
    {
        emit projectReferenceError(requestId, operation, QStringLiteral("引用所属模块、稳定记录键或项目ID无效。"));
        return;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("INSERT OR IGNORE INTO catalog_project_reference VALUES(?,?,?)"));
    query.addBindValue(ownerModule);
    query.addBindValue(referenceKey);
    query.addBindValue(projectId);
    if (!query.exec())
    {
        emit projectReferenceError(requestId, operation, sqlError(query));
        return;
    }
    query.prepare(QStringLiteral("SELECT project_id FROM catalog_project_reference WHERE owner_module=? AND reference_key=?"));
    query.addBindValue(ownerModule);
    query.addBindValue(referenceKey);
    if (!query.exec() || !query.next() || query.value(0).toLongLong() != projectId)
    {
        emit projectReferenceError(requestId, operation, QStringLiteral("该外部记录键已关联其他项目或核对失败，请先核对所属记录。"));
        return;
    }
    emit projectReferenceSucceeded(requestId, operation);
}
void ProjectsStore::releaseProjectReference(qint64 requestId, QString ownerModule, QString referenceKey)
{
    const QString operation = QStringLiteral("releaseProjectReference");
    if (m_original)
    {
        emit projectReferenceError(requestId, operation, QStringLiteral("原库模式不释放另一份catalog中的引用，防止同号项目被误解绑。"));
        return;
    }
    static const QRegularExpression module(QStringLiteral("^[a-z][a-z0-9_]{0,31}\\z"));
    if (QThread::currentThread() != thread() || !m_ready || !m_session.authenticated ||
        !m_session.actionReady || m_session.role < 1 || m_session.role > 5 ||
        !module.match(ownerModule).hasMatch() || referenceKey.isEmpty() || referenceKey.size() > 128 ||
        referenceKey != referenceKey.trimmed() || referenceKey.contains(QChar::Null))
    {
        emit projectReferenceError(requestId, operation, QStringLiteral("引用释放条件无效；须先确认所属外部记录已删除或保存失败。"));
        return;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM catalog_project_reference WHERE owner_module=? AND reference_key=?"));
    query.addBindValue(ownerModule);
    query.addBindValue(referenceKey);
    if (!query.exec())
    {
        emit projectReferenceError(requestId, operation, sqlError(query));
        return;
    }
    emit projectReferenceSucceeded(requestId, operation);
}
void ProjectsStore::checkProjectReferences(qint64 requestId, qint64 projectId)
{
    const QString operation = QStringLiteral("checkProjectReferences");
    if (m_original)
    {
        if (!check(requestId, operation)) return;
        ProjectRecords records;
        ProjectUnits units;
        QString message;
        QStringList references;
        if (!read(&records, &units, &message) || !m_original->references(projectId, &references, &message))
        { emit projectReferenceError(requestId, operation, message); return; }
        const bool exists = std::any_of(records.cbegin(), records.cend(),
            [projectId](const ProjectRecord &record) { return record.id == projectId; });
        emit projectReferencesReady(requestId, projectId, exists, references);
        return;
    }
    if (QThread::currentThread() != thread() || !m_ready || projectId <= 0 || !m_database.transaction())
    {
        emit projectReferenceError(requestId, operation, QStringLiteral("项目引用检查不可用，禁止假定未引用。"));
        return;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT id FROM project_base WHERE id=?"));
    query.addBindValue(projectId);
    if (!query.exec())
    {
        m_database.rollback();
        emit projectReferenceError(requestId, operation, sqlError(query));
        return;
    }
    const bool exists = query.next();
    query.prepare(QStringLiteral("SELECT owner_module,reference_key FROM catalog_project_reference WHERE project_id=? ORDER BY owner_module,reference_key"));
    query.addBindValue(projectId);
    QStringList references;
    if (!query.exec())
    {
        m_database.rollback();
        emit projectReferenceError(requestId, operation, sqlError(query));
        return;
    }
    while (query.next()) references.append(query.value(0).toString() + QStringLiteral(":") + query.value(1).toString());
    if (query.lastError().isValid())
    {
        const QString message = sqlError(query);
        query.finish();
        m_database.rollback();
        emit projectReferenceError(requestId, operation, message);
        return;
    }
    query.finish();
    if (!m_database.commit())
    {
        m_database.rollback();
        emit projectReferenceError(requestId, operation, m_database.lastError().text());
        return;
    }
    emit projectReferencesReady(requestId, projectId, exists, references);
}
void ProjectsStore::close()
{
    if (QThread::currentThread() != thread())
    {
        return;
    }
    release();
    m_session = AccountSession();
    emit closed();
}
