#include "report_document.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <cmath>
#include <algorithm>

namespace
{
const int kMaximumRows = 384; // History store: 64 tasks, result types 1..6.
const QStringList kHeaderKeys{QStringLiteral("hospital_name"), QStringLiteral("name"),
    QStringLiteral("sex"), QStringLiteral("age"), QStringLiteral("bed_number"),
    QStringLiteral("department"), QStringLiteral("sample_provider"), QStringLiteral("sample_sn"),
    QStringLiteral("patient_type"), QStringLiteral("report_date"), QStringLiteral("mark"),
    QStringLiteral("checker"), QStringLiteral("reviewer")};
const QStringList kRowKeys{QStringLiteral("project"), QStringLiteral("project_name"),
    QStringLiteral("project_result"), QStringLiteral("reference_range"), QStringLiteral("unit")};

bool validateText(const QString &text)
{
    return text.size() <= 4096 && !text.contains(QChar::Null);
}
}

bool HistoryReportDocument::anonymousOptions(HistoryPrintOptions *options, QString *error)
{
    QFile file(QStringLiteral(":/history_report/anonymous_options.json"));
    if (!file.open(QIODevice::ReadOnly))
    {
        *error = QStringLiteral("内嵌匿名报告配置缺失。");
        return false;
    }
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll());
    if (!json.isObject() || json.object().value(QStringLiteral("schema")).toString() != QStringLiteral("sn100-anonymous-print-v1"))
    {
        *error = QStringLiteral("匿名报告配置版本错误。");
        return false;
    }
    HistoryPrintOptions result;
    result.hospitalName = json.object().value(QStringLiteral("hospital_name")).toString();
    for (const QJsonValue &value : json.object().value(QStringLiteral("columns")).toArray())
    {
        const QJsonObject item = value.toObject();
        HistoryPrintColumn column;
        column.project = item.value(QStringLiteral("project")).toString();
        column.resultType = item.value(QStringLiteral("result_type")).toInt(-1);
        column.description = item.value(QStringLiteral("description")).toString();
        column.enabled = item.value(QStringLiteral("enabled")).toBool(true);
        column.mainResult = item.value(QStringLiteral("main_result")).toBool(true);
        // No clinical reference intervals are invented for the default fixture.
        column.hasReference = item.contains(QStringLiteral("lower")) && item.contains(QStringLiteral("upper"));
        column.lower = item.value(QStringLiteral("lower")).toDouble();
        column.upper = item.value(QStringLiteral("upper")).toDouble();
        result.columns.append(column);
    }
    if (result.hospitalName.isEmpty() || result.columns.isEmpty() || result.columns.size() > 64)
    {
        *error = QStringLiteral("匿名医院/报告列配置为空或超出容量。");
        return false;
    }
    *options = result;
    return true;
}

bool HistoryReportDocument::build(const HistoryReport &report, const HistoryPrintOptions &options,
                                 HistoryPrintDocument *document, QString *error)
{
    if (report.id <= 0 || report.results.isEmpty() || report.results.size() > 64 ||
        (!options.originalProfile && options.hospitalName.isEmpty()))
    {
        *error = QStringLiteral("所选匿名报告为空、标识无效或超过64任务容量。");
        return false;
    }
    HistoryPrintDocument result;
    if (options.originalProfile)
    {
        result.originalTemplateXml = report.results.first().originalTemplateXml;
        if (result.originalTemplateXml.isEmpty())
        { *error = QStringLiteral("原resource/report_template/report.xml缺失，未使用匿名模板。 "); return false; }
        for (const auto &task : report.results)
            if (task.originalTemplateXml != result.originalTemplateXml)
            { *error = QStringLiteral("原报告任务的模板快照不一致，请重新读取。 "); return false; }
    }
    const HistoryPatient &patient = report.patient;
    result.headers = {{QStringLiteral("hospital_name"), options.hospitalName},
        {QStringLiteral("name"), patient.name},
        {QStringLiteral("sex"), patient.sex == 0 || patient.sex == 1
            ? HistoryStore::sexText(patient.sex) : QStringLiteral("Unknown")},
        {QStringLiteral("age"), QString::number(patient.age)}, {QStringLiteral("bed_number"), patient.bedNumber},
        {QStringLiteral("department"), patient.department}, {QStringLiteral("sample_provider"), patient.sender},
        {QStringLiteral("sample_sn"), options.originalProfile ? report.barcode : QString::number(report.sampleNumber)},
        {QStringLiteral("patient_type"), patient.patientType},
        {QStringLiteral("report_date"), report.dateTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))},
        {QStringLiteral("mark"), patient.remark}, {QStringLiteral("checker"), patient.examiner},
        {QStringLiteral("reviewer"), patient.auditor}};
    for (auto it = result.headers.cbegin(); it != result.headers.cend(); ++it)
    {
        if (!validateText(it.value()) || it.value().contains('\n') || it.value().contains('\r'))
        {
            *error = QStringLiteral("原模板单行字段%1含换行/无效文本；未生成截断报告，请先核对患者信息。").arg(it.key());
            return false;
        }
    }
    // Original QtReport prints configured individual task results, not the
    // history screen's mean/CV rows. Keep every repeated task and raw DB intact.
    QStringList projects;
    for (const HistoryPrintColumn &column : options.columns)
    {
        if (!projects.contains(column.project)) projects.append(column.project);
    }
    for (const QString &project : projects)
    {
        for (const HistoryResult &task : report.results)
        {
            if (task.project != project) continue;
            const auto &values = options.originalProfile ? task.storedValues : task.values;
            for (const HistoryResultValue &value : values)
            {
                const HistoryPrintColumn *configuration = nullptr;
                for (const HistoryPrintColumn &candidate : options.columns)
                    if (candidate.project == task.project && candidate.resultType == value.resultType)
                        configuration = &candidate;
                if (!configuration || !configuration->enabled ||
                    (value.resultType == 1 && !configuration->mainResult)) continue;
                const HistoryPrintColumn &column = *configuration;
                if (value.decimals < 0 || value.decimals > 12 || !std::isfinite(value.factor))
                {
                    *error = QStringLiteral("项目%1的报告精度/单位配置未知；不会猜测打印格式。").arg(column.project);
                    return false;
                }
                QString reference;
                QString arrow;
                bool numeric = false;
                const double raw = value.rawValue.toDouble(&numeric);
                if (column.hasReference)
                {
                    if (!std::isfinite(column.lower) || !std::isfinite(column.upper) || column.lower > column.upper)
                    {
                        *error = QStringLiteral("匿名报告参考区间配置无效。");
                        return false;
                    }
                    reference = QString::number(column.lower, 'f', value.decimals) + QChar('-')
                        + QString::number(column.upper, 'f', value.decimals);
                    // Compatibility: legacy wrapper compares raw values with
                    // configured reference bounds, then applies unit display.
                    if (numeric && std::isfinite(raw))
                    {
                        if (raw < column.lower) arrow = QStringLiteral(" ↓");
                        else if (raw > column.upper) arrow = QStringLiteral(" ↑");
                    }
                }
                QString formatted = HistoryStore::displayValue(value);
                if (options.originalProfile)
                {
                    bool originalNumeric = false;
                    const float original = value.rawValue.toFloat(&originalNumeric);
                    const double scaled = value.rawValue.toDouble() * value.originalPrintFactor;
                    formatted = originalNumeric && std::isfinite(original) && std::isfinite(scaled)
                        ? QString::number(scaled, 'f', value.decimals) : QStringLiteral("****");
                    // 原QtReport比较toFloat原值，再显示单位换算值；无箭头也保留末尾空格。
                    arrow = QString();
                    if (formatted != QStringLiteral("****"))
                    {
                        if (original < column.lower) arrow = QStringLiteral("↓");
                        else if (original > column.upper) arrow = QStringLiteral("↑");
                    }
                    formatted += QLatin1Char(' ');
                }
                if (formatted == QStringLiteral("****")) arrow.clear();
                result.rows.append({{QStringLiteral("project"), task.project},
                    {QStringLiteral("project_name"), column.description},
                    {QStringLiteral("project_result"), formatted + arrow},
                    {QStringLiteral("reference_range"), reference}, {QStringLiteral("unit"), value.unit}});
                if (result.rows.size() > kMaximumRows)
                {
                    *error = QStringLiteral("报告超过384行容量；拒绝截断。");
                    return false;
                }
            }
        }
    }
    // An unconfigured source value is not silently dropped. Explicitly disabled
    // anonymous columns (e.g. RAWX) are the only allowed omission.
    for (const HistoryResult &task : report.results)
        for (const HistoryResultValue &value : task.values)
        {
            if (options.originalProfile) continue; // 原QtReport跳过没有项目参数的旧结果，数据库原串仍保留。
            bool configured = false;
            for (const HistoryPrintColumn &column : options.columns)
                if (column.project == task.project && column.resultType == value.resultType) configured = true;
            if (!configured)
            {
                *error = QStringLiteral("项目%1/结果类型%2缺少打印列契约。").arg(task.project).arg(value.resultType);
                return false;
            }
        }
    if (result.rows.isEmpty())
    {
        *error = QStringLiteral("当前报告没有已配置且启用的打印结果。");
        return false;
    }
    *document = result;
    return true;
}

bool HistoryReportDocument::originalOptions(const HistoryReport &report, HistoryPrintOptions *options, QString *error)
{
    HistoryPrintOptions result;
    result.originalProfile = true;
    // 原QtReport::setValue没有yiyuanmingcheng分支：模板的此占位实际为空，先保持原行为。
    // 医院页及主窗仍按原INI显示名称；更改报告标题需要后续明确业务要求。
    QSet<QString> known;
    HistoryResults ordered = report.results;
    std::stable_sort(ordered.begin(), ordered.end(), [](const HistoryResult &left, const HistoryResult &right)
    { return left.originalProjectOrder < right.originalProjectOrder; });
    for (const auto &task : ordered)
    {
        if (!task.originalProfile) { *error = QStringLiteral("原报告混入了其他来源的结果，请重新读取。 "); return false; }
        if (!task.originalProjectEnabled) continue;
        for (const auto &value : task.storedValues)
        {
            const QString key = task.project + QChar(0x1f) + QString::number(value.resultType);
            if (known.contains(key) || !value.selectionKnown) continue;
            known.insert(key);
            HistoryPrintColumn column;
            column.project = task.project; column.resultType = value.resultType;
            column.description = task.originalProjectFullName;
            column.enabled = value.used; column.mainResult = value.mainResult;
            column.hasReference = value.originalReferenceKnown;
            column.lower = value.originalReferenceLow; column.upper = value.originalReferenceHigh;
            result.columns.append(column);
        }
    }
    if (result.columns.isEmpty()) { *error = QStringLiteral("原报告没有可对应的原项目打印参数，未套用匿名项目。 "); return false; }
    *options = result;
    return true;
}

QJsonObject HistoryReportDocument::toJson(const HistoryPrintDocument &document)
{
    QJsonObject headers;
    for (auto it = document.headers.cbegin(); it != document.headers.cend(); ++it) headers.insert(it.key(), it.value());
    QJsonArray rows;
    for (const auto &row : document.rows)
    {
        QJsonObject item;
        for (auto it = row.cbegin(); it != row.cend(); ++it) item.insert(it.key(), it.value());
        rows.append(item);
    }
    QJsonObject result{{QStringLiteral("schema"), QStringLiteral("sn100-render-snapshot-v1")},
        {QStringLiteral("headers"), headers}, {QStringLiteral("rows"), rows}};
    if (!document.originalTemplateXml.isEmpty())
        result.insert(QStringLiteral("original_template_xml"), QString::fromLatin1(document.originalTemplateXml.toBase64()));
    return result;
}

bool HistoryReportDocument::fromJson(const QJsonObject &json, HistoryPrintDocument *document, QString *error)
{
    if (json.value(QStringLiteral("schema")).toString() != QStringLiteral("sn100-render-snapshot-v1"))
    {
        *error = QStringLiteral("报告渲染快照版本错误。");
        return false;
    }
    HistoryPrintDocument result;
    if (json.contains(QStringLiteral("original_template_xml")))
    {
        const auto encoded = json.value(QStringLiteral("original_template_xml")).toString().toLatin1();
        result.originalTemplateXml = QByteArray::fromBase64(encoded);
        if (result.originalTemplateXml.isEmpty() || result.originalTemplateXml.size() > 256 * 1024 ||
            result.originalTemplateXml.toBase64() != encoded)
        { *error = QStringLiteral("原报表模板快照无效或超过256KiB，未猜测替代。 "); return false; }
    }
    const QJsonObject headers = json.value(QStringLiteral("headers")).toObject();
    for (const QString &key : kHeaderKeys)
    {
        if (!headers.value(key).isString() || !validateText(headers.value(key).toString()))
        {
            *error = QStringLiteral("报告头字段缺失或无效：") + key;
            return false;
        }
        result.headers.insert(key, headers.value(key).toString());
    }
    const QJsonArray rows = json.value(QStringLiteral("rows")).toArray();
    if (rows.isEmpty() || rows.size() > kMaximumRows)
    {
        *error = QStringLiteral("报告渲染行数无效。");
        return false;
    }
    for (const QJsonValue &value : rows)
    {
        QMap<QString, QString> row;
        for (const QString &key : kRowKeys)
        {
            const QJsonValue field = value.toObject().value(key);
            if (!field.isString() || !validateText(field.toString()))
            {
                *error = QStringLiteral("报告结果字段无效：") + key;
                return false;
            }
            row.insert(key, field.toString());
        }
        result.rows.append(row);
    }
    *document = result;
    return true;
}
