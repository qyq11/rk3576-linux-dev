#include "original_control_tasks.h"
#include <QDateTime>
#include <QDir>
#include <QSqlError>
#include <QSqlQuery>
#include <QUrl>
#include <limits>

namespace OriginalControlTasks
{
namespace
{
bool fail(QString *message, const QString &text)
{ if (message) *message = text; return false; }
bool query(QSqlQuery *query, const QString &sql, const QVariantList &bindings, QString *message)
{
    if (!query->prepare(sql)) return fail(message, query->lastError().text());
    for (const auto &value : bindings) query->addBindValue(value);
    return query->exec() || fail(message, query->lastError().text());
}
}

bool assignCalibration(const OriginalReagentRepository &repository, const OriginalReagentState &state,
                       bool linuxNew, OriginalTasks::Task *task, QMap<qint64, double> *remaining, QString *message)
{
    if (!task || task->type != QStringLiteral("CAL")) return fail(message, QStringLiteral("不是已确认的校准任务。"));
    const QString selectedGroup = task->calibration.reagentBatchGroup;
    if (selectedGroup.isEmpty()) return fail(message, QStringLiteral("原校准任务没有试剂组。"));
    OriginalReagentState allocation = state;
    const bool related = task->project.originalBaseValues.value(QStringLiteral("is_related")).toInt() > 0;
    if (!linuxNew && related && selectedGroup != QStringLiteral("Default"))
    {
        QSet<QString> needed;
        for (const auto &step : task->project.advanced.reagents) needed.insert(step.value(QStringLiteral("reagent_name")));
        for (int index = allocation.holes.size() - 1; index >= 0; --index)
        {
            const auto &hole = allocation.holes[index];
            if (hole.value("area").toString() != QStringLiteral("r_area") || !needed.contains(hole.value("reagent_name").toString())) continue;
            const QString batch = hole.value("reagent_batch").toString();
            // 原旧协议用contains，而非按分隔符重解释批号；空批号按Default核对。
            if (!selectedGroup.contains(batch.isEmpty() ? QStringLiteral("Default") : batch)) allocation.holes.removeAt(index);
        }
    }
    // 原LinuxNew校准无批组筛瓶，旧协议Default亦按优先级选全部瓶。
    // 这里只修改分配用的值副本，不改项目配置或数据库。
    const auto project = task->project;
    task->project.originalBaseValues[QStringLiteral("is_related")] = QStringLiteral("0");
    const bool ok = repository.assignTask(allocation, task, remaining, message);
    task->project = project;
    task->batchGroup = selectedGroup;
    return ok;
}

bool attachCalibration(OriginalBusiness::Database &database, const QString &root, QString *message)
{
    const QString uri = QUrl::fromLocalFile(QDir(root).filePath(QStringLiteral("result/calibration.db"))).toString(QUrl::FullyEncoded) + QStringLiteral("?mode=rw");
    if (!database.execute(QStringLiteral("ATTACH DATABASE ? AS original_calibration"), {uri}, message)) return false;
    QSqlQuery mode(database.db);
    if (!mode.exec(QStringLiteral("PRAGMA original_calibration.journal_mode")) || !mode.next()) return fail(message, mode.lastError().text());
    const QString value = mode.value(0).toString().toLower();
    if (value != QStringLiteral("delete") && value != QStringLiteral("truncate") && value != QStringLiteral("persist"))
        return fail(message, QStringLiteral("原校准库当前事务模式不能与任务号、报告及试剂一起恢复，未更改库模式。"));
    return true;
}

bool reserveCalibration(OriginalBusiness::Database &database, QList<OriginalTasks::Task> *tasks, QString *message)
{
    QSqlQuery counter(database.db);
    if (!counter.exec(QStringLiteral("SELECT id,total_count FROM original_calibration.calibraiton_total_count ORDER BY id")) || !counter.next())
        return fail(message, QStringLiteral("原calibraiton_total_count计数不存在，未另造编号表。"));
    const qint64 counterRow = counter.value(0).toLongLong();
    bool valid = false; qint64 next = counter.value(1).toLongLong(&valid);
    if (!valid || next < 1 || counter.next()) return fail(message, QStringLiteral("原校准编号计数无效或不唯一，未重置编号。"));
    counter.finish();
    QSqlQuery maximum(database.db);
    if (!maximum.exec(QStringLiteral("SELECT COALESCE(MAX(calibration_id),0) FROM (SELECT calibration_id FROM original_calibration.calibration_base UNION ALL SELECT calibration_id FROM original_calibration.calibration_data)")) || !maximum.next() || maximum.value(0).toLongLong() >= next)
        return fail(message, QStringLiteral("原校准计数未超过已有曲线编号，未覆盖原记录。"));
    maximum.finish();
    qint64 firstTask = std::numeric_limits<qint64>::max();
    for (const auto &task : *tasks) firstTask = qMin(firstTask, task.taskId);
    if (!maximum.exec(QStringLiteral("SELECT only_count FROM original_calibration.calibration_data")))
        return fail(message, maximum.lastError().text());
    while (maximum.next())
        for (const auto &text : maximum.value(0).toString().split('|', Qt::KeepEmptyParts))
        {
            if (text.isEmpty()) continue;
            bool ok = false; const qint64 id = text.toLongLong(&ok);
            if (!ok || id < 0 || id >= firstTask)
                return fail(message, QStringLiteral("原校准任务号损坏或与新占号冲突，未重复分配任务。"));
        }
    if (maximum.lastError().isValid()) return fail(message, maximum.lastError().text());
    maximum.finish();
    const qint64 previous = next;
    QMap<qint64, qint64> curveIds;
    QMap<QPair<qint64, int>, QList<int>> pointTasks;
    for (int index = 0; index < tasks->size(); ++index)
    {
        auto &task = (*tasks)[index];
        if (task.type != QStringLiteral("CAL")) continue;
        if (!curveIds.contains(task.project.id))
        {
            if (next >= std::numeric_limits<int>::max()) return fail(message, QStringLiteral("原校准编号已耗尽。"));
            task.calibrationId = next++;
            curveIds[task.project.id] = task.calibrationId;
            QString resultUnit, concentrationUnit;
            for (const auto &result : task.project.results)
            {
                if (result.type == QStringLiteral("Original")) resultUnit = result.unit;
                if (result.type == QStringLiteral("Calibration")) concentrationUnit = result.unit;
            }
            const auto &base = task.project.originalBaseValues;
            if (!database.execute(QStringLiteral("INSERT INTO original_calibration.calibration_base(calibration_id,project_name,calibration_datetime,boold_batch,point_size,repeat_number,calculate_id,is_checked,is_edited,result_unit,con_unit,x_change,y_change,visible) VALUES(?,?,?,?,?,?,?,0,0,?,?,?,?,1)"),
                {task.calibrationId, task.project.name, QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddHHmmss")),
                 task.calibration.calibratorBatch, task.calibration.pointCount, task.calibration.repeatCount,
                 base.value(QStringLiteral("calculate_select")), resultUnit, concentrationUnit,
                 base.value(QStringLiteral("x_change")), base.value(QStringLiteral("y_change"))}, message, 1)) return false;
        }
        else task.calibrationId = curveIds.value(task.project.id);
        // 原点表按数字点号插入，旧程序还会按自增id读取，10号不能排到2号前。
        pointTasks[qMakePair(task.calibrationId, task.calibrationPoint)].append(index);
    }
    for (const auto &indices : pointTasks)
    {
        const auto &first = tasks->at(indices.first());
        QStringList taskIds;
        for (const auto index : indices)
        {
            const auto &task = tasks->at(index);
            if (task.calibrationConcentration != first.calibrationConcentration || task.rate != first.rate ||
                task.calibration.groupToken != first.calibration.groupToken)
                return fail(message, QStringLiteral("同校准点的浓度、比例或草稿身份不一致，未占号。"));
            taskIds.append(QString::number(task.taskId));
        }
        if (!database.execute(QStringLiteral("INSERT INTO original_calibration.calibration_data(calibration_id,point_index,sample_rate,concentration,result_flag,result_value,only_count) VALUES(?,?,?,?,'','',?)"),
            {first.calibrationId, first.calibrationPoint, first.rate, first.calibrationConcentration, taskIds.join('|')}, message, 1)) return false;
    }
    return database.execute(QStringLiteral("UPDATE original_calibration.calibraiton_total_count SET total_count=? WHERE id=? AND total_count=?"), {next, counterRow, previous}, message, 1);
}

bool calibrationState(OriginalBusiness::Database &database, const OriginalTasks::Task &task, const QString &state, QString *message)
{
    QSqlQuery point(database.db);
    if (!query(&point, QStringLiteral("SELECT d.id,d.only_count,d.result_value,d.result_flag,d.sample_rate,d.concentration,b.project_name,b.boold_batch,b.is_checked FROM original_calibration.calibration_data d JOIN original_calibration.calibration_base b ON b.calibration_id=d.calibration_id WHERE d.calibration_id=? AND d.point_index=?"),
               {task.calibrationId, task.calibrationPoint}, message) || !point.next())
        return fail(message, QStringLiteral("回包对应的原校准点不存在，未生成替代记录。"));
    const qint64 id = point.value(0).toLongLong();
    const QStringList tasks = point.value(1).toString().split('|', Qt::KeepEmptyParts);
    const QVariant oldValues = point.value(2), oldFlags = point.value(3);
    QStringList values = oldValues.toString().split('|', Qt::KeepEmptyParts), flags = oldFlags.toString().split('|', Qt::KeepEmptyParts);
    const int index = tasks.indexOf(QString::number(task.taskId));
    if (index < 0 || tasks.count(QString::number(task.taskId)) != 1 || point.value(4).toString() != task.rate ||
        point.value(5).toString() != task.calibrationConcentration || point.value(6).toString() != task.project.name ||
        point.value(7).toString() != task.calibration.calibratorBatch || point.value(8).toInt() != 0 || point.next())
        return fail(message, QStringLiteral("校准回包的任务号、点、比例、浓度、项目或批号不符，或记录已审核，未覆盖。"));
    point.finish();
    if (state.isEmpty()) return true; // 只核身份，不写原始点或算法结果。
    while (values.size() < tasks.size()) values.append(QString());
    while (flags.size() < tasks.size()) flags.append(QString());
    bool measured = false;
    values[index].toDouble(&measured);
    if (measured || (!values[index].isEmpty() && flags[index] != QStringLiteral("4") && flags[index] != QStringLiteral("5")))
        return fail(message, QStringLiteral("原校准点已有测量或编辑结果，未用待计算状态覆盖。"));
    values[index] = state;
    // 原4表示非计算状态；曲线到达仍为未知5，不能当正常结果0或审核通过。
    flags[index] = state == QStringLiteral("待计算") ? QStringLiteral("5") : QStringLiteral("4");
    return database.execute(QStringLiteral("UPDATE original_calibration.calibration_data SET result_value=?,result_flag=? WHERE id=? AND result_value IS ? AND result_flag IS ?"),
                            {values.join('|'), flags.join('|'), id, oldValues, oldFlags}, message, 1);
}
}
