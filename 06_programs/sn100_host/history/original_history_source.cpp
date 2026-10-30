#include "original_history_source.h"
#include "../original_files/original_business_database.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <cmath>
#include <algorithm>

namespace
{
QString resultKey(const QString &project, int type)
{ return QStringLiteral("result:") + project + QChar(0x1f) + QString::number(type); }
QString patientName(const QString &key)
{
    if (key == QStringLiteral("patient:name")) return QStringLiteral("Name");
    if (key == QStringLiteral("patient:sex")) return QStringLiteral("Sex");
    if (key == QStringLiteral("patient:age")) return QStringLiteral("Age");
    if (key == QStringLiteral("patient:state")) return QStringLiteral("Result");
    return {};
}
QString patientKey(const QString &name)
{
    for (const auto &key : {"name", "sex", "age", "state"})
    {
        const QString value = QStringLiteral("patient:") + QString::fromLatin1(key);
        if (patientName(value) == name) return value;
    }
    return {};
}
bool fail(QString *message, const QString &text)
{ if (message) *message = text; return false; }
}

QString OriginalHistorySource::databasePath() const
{ return QDir(root_).filePath(QStringLiteral("result/results.db")); }

bool OriginalHistorySource::initialize(const QString &root, QString *message)
{
    if (!OriginalBusiness::workingRoot(root, message)) return false;
    if (!projects_.initialize(root, QString(), message)) return false;
    root_ = QFileInfo(root).canonicalFilePath();
    return refresh(0, message);
}

bool OriginalHistorySource::refresh(quint64 revision, QString *message)
{
    templateXml_.clear();
    const QString templatePath = QDir(root_).filePath(QStringLiteral("resource/report_template/report.xml"));
    if (QFileInfo::exists(templatePath))
    {
        for (QFileInfo current(templatePath); ; current.setFile(current.absolutePath()))
        {
            if (current.isSymLink()) return fail(message, QStringLiteral("原报表模板路径含链接，请核对工作资料。"));
            if (current.absoluteFilePath() == current.absolutePath()) break;
        }
        QFile file(templatePath);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 256 * 1024)
            return fail(message, QStringLiteral("原报表模板不能完整读取，未替换成另一份模板。"));
        templateXml_ = file.readAll();
        if (file.error() != QFile::NoError) return fail(message, QStringLiteral("原报表模板读取未完成。"));
    }
    QStringList reagents, cleaners, diluents;
    if (!projects_.load(&records_, &units_, &reagents, &cleaners, &diluents, message)) return false;
    QCryptographicHash fingerprint(QCryptographicHash::Sha256);
    const QString unitsHash = OriginalProjectRepository::databaseFingerprint(projects_.unitsDatabasePath(), message);
    if (unitsHash.isEmpty()) return false;
    fingerprint.addData(root_.toUtf8()); fingerprint.addData(unitsHash.toLatin1());
    for (const auto &record : records_)
    {
        fingerprint.addData(record.originalFileName.toUtf8());
        fingerprint.addData(record.originalFingerprint.toLatin1());
    }
    source_ = HistoryCurrentFormatSource();
    source_.historyDatasetUuid = QString::fromLatin1(QCryptographicHash::hash(databasePath().toUtf8(), QCryptographicHash::Sha256).toHex());
    source_.catalogDirectory = root_;
    source_.catalogDatabasePath = projects_.unitsDatabasePath();
    source_.catalogVersion = QStringLiteral("sn100-original-projects-v1");
    source_.formatVersion = QStringLiteral("sn100-original-project-result-format-v1");
    source_.fingerprint = QString::fromLatin1(fingerprint.result().toHex());
    source_.requestRevision = revision;
    source_.sourceGeneration = ++generation_;
    source_.capturedAtUtc = QDateTime::currentDateTimeUtc();
    source_.configurationNotice = QStringLiteral("按当前原项目参数显示，原结果字符串保留；不执行结果计算。 ");
    return true;
}

bool OriginalHistorySource::readResults(QSqlDatabase &db, qint64 rowId,
                                        HistoryResults *results, QString *message) const
{
    results->clear();
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT task_id,project_name,data_time,abnormal_type,original_value,"
        "calibration_value,rate_value,inr_value,mda_value,formula_value FROM report_results WHERE fid=? ORDER BY task_id LIMIT 65"));
    query.addBindValue(rowId);
    if (!query.exec()) return fail(message, query.lastError().text());
    while (query.next())
    {
        if (results->size() >= 64) return fail(message, QStringLiteral("一份原报告超过64任务，未截断。"));
        HistoryResult result;
        result.originalProfile = true;
        result.originalTemplateXml = templateXml_;
        result.taskId = query.value(0).toLongLong(); result.project = query.value(1).toString();
        result.testedAt = QDateTime::fromString(query.value(2).toString(), QStringLiteral("yyyyMMddHHmmss"));
        result.abnormalType = query.value(3).toString();
        const auto project = std::find_if(records_.cbegin(), records_.cend(),
            [&](const ProjectRecord &entry) { return entry.name == result.project; });
        if (project != records_.cend())
        {
            result.projectId = project->id; result.instrumentProjectId = project->instrumentProjectId;
            result.originalProjectEnabled = project->enabled; result.originalProjectFullName = project->fullName;
            result.originalProjectOrder = static_cast<int>(std::distance(records_.cbegin(), project));
        }
        for (int type = 1; type <= 6; ++type)
        {
            HistoryResultValue value;
            value.resultType = type; value.sourceResultId = type;
            value.rawValue = query.value(type + 3).toString();
            value.currentSource = source_;
            value.formatOrigin = HistoryValueFormatOrigin::UnknownCurrent;
            value.label = result.project + QStringLiteral(" / ") + QString::number(type);
            value.formatMessage = QStringLiteral("原结果保留；对应原项目或结果配置不存在。 ");
            if (project != records_.cend()) for (const auto &configuration : project->results)
            {
                if (configuration.sourceResultId != type) continue;
                value.label = configuration.name; value.unit = configuration.unit;
                value.decimals = configuration.decimals;
                value.selectionKnown = configuration.selectionKnown;
                value.used = configuration.used; value.display = configuration.display;
                value.mainResult = configuration.mainResult;
                value.originalReferenceKnown = true;
                value.originalReferenceLow = configuration.normalLow; value.originalReferenceHigh = configuration.normalHigh;
                if (type == 2 || type == 3)
                {
                    const auto unit = std::find_if(units_.cbegin(), units_.cend(),
                        [&](const ProjectUnit &candidate) { return candidate.name == value.unit; });
                    if (unit != units_.cend()) value.originalPrintFactor = unit->originalRatio.toInt();
                }
                // 与已移植的原报告显示相同：只解释有据的数值/精度；原字符串不回写。
                double factor = 1;
                if (type == 2 || type == 3)
                {
                    const auto unit = std::find_if(units_.cbegin(), units_.cend(),
                        [&](const ProjectUnit &candidate) { return candidate.name == value.unit; });
                    if (unit == units_.cend() || !std::isfinite(unit->ratio) || unit->ratio <= 0)
                    { value.formatMessage = QStringLiteral("原单位换算未明确，显示原值。 "); break; }
                    factor = unit->ratio;
                }
                if (value.decimals < 0 || value.decimals > 12) break;
                value.factor = factor;
                // 原窗口只格式化有限数值，异常文本和空串保持原样。
                bool numeric = false;
                const double number = value.rawValue.toDouble(&numeric);
                value.currentDisplayValue = numeric && std::isfinite(number) && std::isfinite(number*factor)
                    ? QString::number(number*factor,'f',value.decimals) : value.rawValue;
                value.formatOrigin = HistoryValueFormatOrigin::CurrentCommitted;
                value.formatMessage.clear();
                break;
            }
            result.storedValues.append(value);
            if (!value.selectionKnown || (value.used && value.display)) result.values.append(value);
        }
        results->append(result);
    }
    return !query.lastError().isValid() || fail(message, query.lastError().text());
}

bool OriginalHistorySource::defaults(HistoryDisplayColumns *columns, QString *message) const
{
    columns->clear();
    columns->append({QStringLiteral("patient:name"),QStringLiteral("姓名"),1,true,false});
    columns->append({QStringLiteral("patient:sex"),QStringLiteral("性别"),1,true,false});
    columns->append({QStringLiteral("patient:age"),QStringLiteral("年龄"),1,true,false});
    columns->append({QStringLiteral("patient:state"),QStringLiteral("状态"),1,true,false});
    for (const auto &project : records_) for (const auto &result : project.results)
    {
        if (!project.enabled || !result.selectionKnown || !result.used || result.sourceResultId > 5) continue;
        if (columns->size() >= 68) return fail(message, QStringLiteral("原显示结果超过64列，未截断。"));
        columns->append({resultKey(project.name,result.sourceResultId),
            result.name + (result.unit.isEmpty() ? QString() : QChar(' ') + result.unit),2,result.display,result.mainResult && result.display});
    }
    return true;
}

bool OriginalHistorySource::display(QSqlDatabase &db, HistoryDisplayColumns *columns,
                                   HistoryDisplayColumns *baseline, QString *message) const
{
    if (!defaults(baseline,message)) return false;
    *columns = *baseline;
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT project_name,table_type,display,result_type,column_index FROM report_column_header ORDER BY table_type,column_index,id")))
        return fail(message,query.lastError().text());
    QMap<QString,HistoryDisplayColumn> available;
    for (const auto &column : *baseline) available.insert(column.key,column);
    HistoryDisplayColumns saved; QSet<QString> seen;
    while (query.next())
    {
        const int group = query.value(1).toInt();
        if (group != 1 && group != 2) continue; // 原明细固定条码/日期不属于当前可编辑的两组。
        const QString name = query.value(0).toString();
        const QString key = group == 1 ? patientKey(name) : resultKey(name,query.value(3).toInt());
        if (!available.contains(key)) continue; // 旧项目表头保留在原表中，不删除。
        if (seen.contains(key)) return fail(message,QStringLiteral("原报告表头重复，请核对原配置。"));
        auto column = available.value(key); column.visible = query.value(2).toBool();
        saved.append(column);seen.insert(key);
    }
    if (query.lastError().isValid()) return fail(message,query.lastError().text());
    for (const auto &column : *baseline) if (!seen.contains(column.key)) saved.append(column);
    *columns = saved;
    return true;
}

bool OriginalHistorySource::saveDisplay(QSqlDatabase &db, HistoryDisplayColumns columns, QString *message) const
{
    HistoryDisplayColumns baseline;
    if (!defaults(&baseline,message)) return false;
    QMap<QString,HistoryDisplayColumn> available;
    for (const auto &column : baseline) available.insert(column.key,column);
    if (columns.size() != baseline.size()) return fail(message,QStringLiteral("原显示列发生变化，请重新读取。"));
    QSet<QString> seen;
    if (!db.transaction()) return fail(message,db.lastError().text());
    auto abort = [&](QString text) { db.rollback(); return fail(message,text); };
    QMap<int,int> positions;
    for (const auto &column : columns)
    {
        if (!available.contains(column.key) || seen.contains(column.key)
            || available.value(column.key).group != column.group || (!column.visible && available.value(column.key).required))
            return abort(QStringLiteral("原显示列无效、重复或试图隐藏主结果。"));
        seen.insert(column.key);
        const QString name = column.group == 1 ? patientName(column.key)
            : column.key.mid(7).section(QChar(0x1f),0,0);
        const int type = column.group == 1 ? 0 : column.key.section(QChar(0x1f),1).toInt();
        QSqlQuery existing(db);
        existing.prepare(QStringLiteral("SELECT id FROM report_column_header WHERE project_name=? AND table_type=? AND result_type=?"));
        existing.addBindValue(name);existing.addBindValue(column.group);existing.addBindValue(type);
        if (!existing.exec()) return abort(existing.lastError().text());
        const bool present = existing.next(); const qint64 id = present ? existing.value(0).toLongLong() : 0;
        if (existing.next()) return abort(QStringLiteral("原报告列身份重复，未更新。"));
        existing.finish();
        QSqlQuery update(db);
        if (present)
        {
            update.prepare(QStringLiteral("UPDATE report_column_header SET display=?,column_index=? WHERE id=?"));
            update.addBindValue(column.visible?1:0);update.addBindValue(positions[column.group]++);update.addBindValue(id);
        }
        else
        {
            update.prepare(QStringLiteral("INSERT INTO report_column_header(project_name,table_type,display,result_type,column_index,display_name) VALUES(?,?,?,?,?,?)"));
            update.addBindValue(name);update.addBindValue(column.group);update.addBindValue(column.visible?1:0);
            update.addBindValue(type);update.addBindValue(positions[column.group]++);update.addBindValue(available.value(column.key).title);
        }
        if (!update.exec() || update.numRowsAffected()!=1) return abort(update.lastError().text());
    }
    if (!db.commit()) return abort(db.lastError().text());
    return true;
}
