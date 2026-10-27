#include "history_store.h"
#include "result_project_references.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <cmath>

HistoryCurrentFormatSource HistoryStore::currentFormatSource(const ProjectCurrentFormatSnapshot &format) const
{
    HistoryCurrentFormatSource source;
    source.historyDatasetUuid = m_resultStoreId;
    source.catalogDirectory = format.directory();
    source.catalogDatabasePath = format.databasePath();
    source.catalogVersion = format.catalogVersion();
    source.formatVersion = format.formatVersion();
    source.fingerprint = format.fingerprint();
    source.sourceGeneration = format.generation();
    source.requestRevision = m_formatRequestRevision;
    source.capturedAtUtc = format.capturedAtUtc();
    source.configurationNotice = format.selectionProfilePresent()
        ? QStringLiteral("按本次读取的当前项目配置显示，非实验时单位快照。 ")
        : QStringLiteral("原选择字段尚未配置；需显式保存本地四槽格式配置。 ");
    return source;
}

bool HistoryStore::readCurrentFormat(ProjectCurrentFormatSnapshot *format, QString *message)
{
    *format = ProjectCurrentFormatSnapshot();
    if (!m_resultIngest || !m_resultProjects || m_resultStoreId.isEmpty())
    { *message = QStringLiteral("本地结果或其真实项目来源未就绪。 "); return false; }
    if (!m_resultProjects->currentFormat(format, message)) return false;
    const QString expected = QFileInfo(QDir(m_resultCatalogDirectory).filePath(QStringLiteral("catalog.db"))).canonicalFilePath();
    if (!format->valid() || format->databasePath() != expected
            || format->directory() != QFileInfo(m_resultCatalogDirectory).canonicalFilePath()
            || format->catalogVersion() != QStringLiteral("sn100-catalog-offline-v1")
            || format->formatVersion() != QStringLiteral("sn100-current-result-format-source-v1")
            || format->fingerprint().size() != 64 || format->generation() == 0)
    {
        *format = ProjectCurrentFormatSnapshot();
        *message = QStringLiteral("当前格式来源路径、版本或读取代次不匹配。 ");
        return false;
    }
    return true;
}

bool HistoryStore::readCurrentResults(qint64 rowId, const ProjectCurrentFormatSnapshot &format,
                                      HistoryResults *results, QString *message)
{
    results->clear();
    QSqlQuery tasks(m_database);
    if (!tasks.prepare(QStringLiteral("SELECT r.task_id,r.project_name,r.date_time,r.abnormal_type,"
        "i.project_id,i.instrument_project_id,i.reference_key,i.task_type,i.mda_name,i.completed "
        "FROM report_results r LEFT JOIN result_task_identity i ON i.task_id=r.task_id WHERE r.fid=? ORDER BY r.task_id LIMIT 65")))
    { *message = tasks.lastError().text(); return false; }
    tasks.addBindValue(rowId);
    if (!tasks.exec()) { *message = tasks.lastError().text(); return false; }
    const auto source = currentFormatSource(format);
    while (tasks.next())
    {
        if (results->size() == 64) { *message = QStringLiteral("单报告超过64任务，未截断。 "); return false; }
        HistoryResult result;
        result.taskId = tasks.value(0).toLongLong();
        result.project = tasks.value(1).toString();
        result.testedAt = QDateTime::fromString(tasks.value(2).toString(), QStringLiteral("yyyyMMddHHmmss"));
        result.abnormalType = tasks.value(3).toString();
        result.projectId = tasks.value(4).toLongLong();
        result.instrumentProjectId = tasks.value(5).toString();
        const bool completed = tasks.value(9).toBool();
        if (result.projectId <= 0 || tasks.value(6).toString() != m_resultStoreId + QChar(':') + QString::number(result.taskId)
                || tasks.value(7).toString() != QStringLiteral("SAM") || !tasks.value(8).toString().isEmpty())
        { *message = QStringLiteral("任务稳定身份、类型或持久引用键失配。 "); return false; }
        bool foundIdentity = false;
        bool projectEnabled = false;
        for (const auto &entry : format.projects()) if (entry.id == result.projectId)
        {
            if (entry.name != result.project || entry.instrumentProjectId != result.instrumentProjectId)
            { *message = QStringLiteral("结果项目与本次已提交配置身份不一致。 "); return false; }
            foundIdentity = true;
            projectEnabled = entry.enabled;
        }
        bool held = false;
        if (!foundIdentity || !m_resultProjects->has(result.projectId, tasks.value(6).toString(), &held, message) || !held)
        { if (message->isEmpty()) *message = QStringLiteral("结果的真实项目关联/引用缺失。 "); return false; }
        QSqlQuery values(m_database);
        if (!values.prepare(QStringLiteral("SELECT result_type,raw_value FROM report_values WHERE task_id=? ORDER BY result_type LIMIT 5")))
        { *message = values.lastError().text(); return false; }
        values.addBindValue(result.taskId);
        if (!values.exec()) { *message = values.lastError().text(); return false; }
        int expectedType = 1;
        while (values.next())
        {
            HistoryResultValue value;
            value.resultType = values.value(0).toInt();
            value.rawValue = values.value(1).toString();
            if (value.rawValue.size() > 4096 || value.rawValue.contains(QChar::Null))
            { *message = QStringLiteral("已存结果超长或含空字符；未截断显示。 "); return false; }
            if (expectedType > 4 || value.resultType != expectedType++)
            { *message = QStringLiteral("完成结果不是连续四槽原串。 "); return false; }
            value.formatOrigin = HistoryValueFormatOrigin::UnknownCurrent;
            value.currentSource = source;
            value.label = result.project + QStringLiteral(" / ") + QString::number(value.resultType);
            value.formatMessage = QStringLiteral("当前项目未配置该四槽；需显式保存本地格式配置。 ");
            const ProjectCurrentFormatEntry *configuration = nullptr;
            for (const auto &entry : format.entries())
                if (entry.projectId == result.projectId && entry.resultType == value.resultType)
                {
                    if (configuration) { *message = QStringLiteral("同一项目四槽配置重复。 "); return false; }
                    configuration = &entry;
                }
            bool selected = projectEnabled;
            if (configuration)
            {
                value.sourceResultId = configuration->sourceResultId;
                value.selectionKnown = configuration->selectionKnown;
                value.used = configuration->used;
                value.display = configuration->display;
                value.mainResult = configuration->mainResult;
                if (!configuration->resultName.isEmpty()) value.label = configuration->resultName;
                value.formatMessage = configuration->unknownReason;
                if (configuration->selectionKnown)
                    selected = configuration->projectEnabled && configuration->used && configuration->display;
                if (configuration->known)
                {
                    value.unit = configuration->unit;
                    value.decimals = configuration->decimals;
                    value.factor = configuration->integerRatio;
                    value.formatOrigin = HistoryValueFormatOrigin::CurrentCommitted;
                    value.currentDisplayValue = value.rawValue;
                    // Original vertical view leaves empty/status/**** text as
                    // text. Only a successfully parsed finite number is scaled.
                    bool numeric = false;
                    double number = value.rawValue.toDouble(&numeric);
                    if (numeric)
                    {
                        if (value.resultType == 2 || value.resultType == 3) number *= configuration->integerRatio;
                        if (!std::isfinite(number))
                        {
                            value.formatOrigin = HistoryValueFormatOrigin::UnknownCurrent;
                            value.unit.clear(); value.decimals = -1; value.factor = 1;
                            value.formatMessage = QStringLiteral("数值或单位换算结果不是有限数；未格式化。 ");
                        }
                        else value.currentDisplayValue = QString::number(number, 'f', configuration->decimals);
                    }
                }
            }
            if (value.formatOrigin == HistoryValueFormatOrigin::UnknownCurrent)
                value.label += QStringLiteral("（格式未知）");
            result.storedValues.append(value);
            if (selected) result.values.append(value);
        }
        if (values.lastError().isValid()) { *message = values.lastError().text(); return false; }
        if (result.storedValues.size() != (completed ? 4 : 0))
        { *message = QStringLiteral("任务完成状态与四槽原串数量不一致。 "); return false; }
        results->append(result);
    }
    if (tasks.lastError().isValid()) { *message = tasks.lastError().text(); return false; }
    return true;
}
