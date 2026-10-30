#include "original_detection_session.h"
#include "original_files/original_ini.h"
#include "original_result_codec.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <QSettings>
#include <QTemporaryFile>
#include <QUrl>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <cmath>
#include <limits>

using namespace OriginalTasks;
using OriginalBusiness::Database;
namespace
{
bool fail(QString *message, const QString &text)
{
    if (message) *message = text;
    return false;
}
bool boolean(const OriginalIni::Settings &settings, const QString &key, bool fallback, bool *result, QString *message)
{
    const QString value = settings.values.value(key, fallback ? QStringLiteral("true") : QStringLiteral("false")).toString().toLower();
    if (value != QStringLiteral("true") && value != QStringLiteral("false") && value != QStringLiteral("0") && value != QStringLiteral("1"))
        return fail(message, QStringLiteral("原配置开关不是true/false/0/1：") + key);
    *result = value == QStringLiteral("true") || value == QStringLiteral("1");
    return true;
}
bool versionMatches(const OriginalReagentState &observed, QString *message)
{
    for (auto i = observed.versions.cbegin(); i != observed.versions.cend(); ++i)
        if (i.value() != OriginalProjectRepository::databaseFingerprint(QDir(observed.root).filePath(i.key()), message))
            return fail(message, QStringLiteral("任务准备后原试剂资料发生变化，请重新读取，不发送旧参数。"));
    return true;
}
bool fullProjectActions(const QString &root, ProjectRecord *project, QString *message)
{
    // 项目编辑页仅展示允许编辑的列；设备旧协议还需要未显示的原列，必须直接读取全行。
    Database database;
    const QString file = QStringLiteral("project/") + project->originalFileName;
    if (!database.open(root, file, false, message)) return false;
    for (bool sample : {true, false})
    {
        auto &rows = sample ? project->advanced.samples : project->advanced.reagents;
        QSqlQuery query(database.db);
        if (!query.exec(sample ? QStringLiteral("SELECT * FROM project_sample_rate ORDER BY xuhao,rowid")
                               : QStringLiteral("SELECT * FROM project_reagent ORDER BY xuhao,rowid")))
            return fail(message, QStringLiteral("原项目设备参数行无法完整读取：") + query.lastError().text());
        const QSqlRecord fields = query.record();
        QList<ProjectActionFields> complete;
        while (query.next())
        {
            if (complete.size() >= 128) return fail(message, QStringLiteral("原项目加样参数超过128行，未截断。"));
            ProjectActionFields values;
            for (int column = 0; column < fields.count(); ++column)
                values.insert(fields.fieldName(column), query.value(column).toString());
            complete.append(values);
        }
        if (query.lastError().isValid()) return fail(message, query.lastError().text());
        rows = complete;
    }
    return project->originalFingerprint == OriginalProjectRepository::databaseFingerprint(QDir(root).filePath(file), message) ||
           fail(message, QStringLiteral("读取完整设备参数时项目已改变，请重新读取。"));
}
bool calibrationGroupsReady(const QString &root, const OriginalReagentState &observed,
                            const QList<Task> &tasks, QString *message)
{
    QSet<QString> projects;
    for (const auto &task : tasks)
        if (task.project.originalBaseValues.value(QStringLiteral("is_related")).toInt() > 0) projects.insert(task.project.name);
    if (projects.isEmpty()) return true;
    Database calibration;
    OriginalBusiness::Rows rows;
    if (!calibration.open(root, QStringLiteral("result/calibration.db"), false, message) ||
        !calibration.rows(QStringLiteral("calibration_batch_group"), {"id","project_name","batch_group","calibration_id"}, &rows, message)) return false;
    for (const auto &project : projects)
    {
        QStringList groups;
        for (const auto &row : observed.batchGroups)
        {
            const QString group = row.value(QStringLiteral("batch_group")).toString();
            if (row.value(QStringLiteral("project_name")).toString() == project && group != QStringLiteral("Default") && group != QStringLiteral("None")) groups.append(group);
        }
        if (groups.isEmpty()) return fail(message, QStringLiteral("项目%1启用了试剂组，但原库没有相应试剂组，按原版规则不能开始。").arg(project));
        for (const auto &group : groups)
        {
            bool found = false;
            for (const auto &row : rows)
            {
                const QString candidate = row.value(QStringLiteral("batch_group")).toString();
                if (row.value(QStringLiteral("project_name")).toString() == project &&
                    (candidate == group || candidate == QStringLiteral("Default") || candidate == QStringLiteral("None")) &&
                    row.value(QStringLiteral("calibration_id")).toLongLong() > 0) found = true;
            }
            if (!found) return fail(message, QStringLiteral("项目%1的%2试剂组未设置校准曲线，按原版规则不能开始。").arg(project,group));
        }
    }
    return true;
}
}

bool OriginalDetectionSession::initialize(OriginalSampleRepository *samples, QString *message)
{
    if (!samples || samples->root().isEmpty()) return fail(message, QStringLiteral("原样本仓库未就绪。"));
    samples_ = samples;
    root_ = samples->root();
    if (!reagents_.initialize(root_, message)) return false;
    OriginalIni::Settings settings, debug, theme;
    if (!OriginalIni::read(QDir(root_).filePath(QStringLiteral("config.ini")), &settings, message) ||
        !OriginalIni::read(QDir(root_).filePath(QStringLiteral("config_Debug.ini")), &debug, message) ||
        !OriginalIni::read(QDir(root_).filePath(QStringLiteral("theme_config.ini")), &theme, message)) return false;
    if (!boolean(settings, QStringLiteral("userOption/isSampleHoleScanClose"), false, &scannerClosed_, message) ||
        !boolean(settings, QStringLiteral("userOption/isSampleScanClose"), false, &rackScannerClosed_, message) ||
        !boolean(settings, QStringLiteral("userOption/isSampleHoleScanOpenHoleNoneWork"), true, &allowMissingBarcode_, message) ||
        !boolean(debug, QStringLiteral("userOption/SendTaskByRowID"), true, &sendByReportRow_, message)) return false;
    const QString report = theme.values.value(QStringLiteral("theme/ReportTableType"), QStringLiteral("0")).toString();
    if (report != QStringLiteral("0") && report != QStringLiteral("1")) return fail(message, QStringLiteral("原报告排版配置不是0或1。"));
    horizontalReport_ = report == QStringLiteral("0");
    return true;
}

bool OriginalDetectionSession::tasksForRack(const SampleRack &rack, const RackRequest *request,
                                          const OriginalReagentState &reagents, QList<Task> *tasks,
                                          QMap<qint64, double> *remaining, QString *message) const
{
    QList<Task> result;
    // 原sendTaskByRowID先遍历同一报告行的重复次数，再按项目表顺序整理项目。
    int repetitions = 1;
    if (sendByReportRow_)
        for (const auto &hole : rack.holes)
            for (const auto &task : hole.tasks) repetitions = qMax(repetitions, task.repeats);
    for (const auto &hole : rack.holes)
    {
        if (!hole.revise || sampleHoleState(sampleHoleTaskStates(hole)) != SampleHoleState::Ready) continue;
        if (hole.sampleType != 0) return fail(message, QStringLiteral("原质控批号与任务尚未接入完成，不能将其当普通样本下发。"));
        QString sample = hole.sampleId;
        if (request)
        {
            const QString code = request->barcodes.value(hole.number);
            if (code.contains(QStringLiteral("empty"), Qt::CaseInsensitive)) continue;
            if (!rack.emergency && !scannerClosed_ &&
                (code.isEmpty() || (code.contains(QStringLiteral("none"), Qt::CaseInsensitive) && !allowMissingBarcode_))) continue;
            const QStringList invalid{QStringLiteral("None"),QStringLiteral("none"),QStringLiteral("empty"),QStringLiteral("Empty"),QStringLiteral("Null"),QString()};
            if (!invalid.contains(code, Qt::CaseInsensitive)) sample = code;
        }
        for (int round = 0; round < repetitions; ++round)
            for (const auto &project : projects_)
                for (const auto &preset : hole.tasks)
                {
                    if (preset.projectId != project.id) continue;
                    const auto states = sampleTaskStates(preset);
                    const int first = sendByReportRow_ ? round : 0;
                    const int last = sendByReportRow_ ? qMin(round + 1, preset.repeats) : preset.repeats;
                    for (int repeat = first; repeat < last; ++repeat)
                    {
                        if (repeat >= states.size() || (states[repeat] != SampleTaskState::Idle && states[repeat] != SampleTaskState::Reset)) continue;
                        Task task;
                        task.taskId = 1; // 仅供准备时完整字段校验；reserve提交原编号前不返回报文。
                        task.reportRow = (horizontalReport_ ? repeat : 0) + hole.number * 1000;
                        task.rack = rack.name;
                        task.hole = hole.number;
                        task.repeatIndex = repeat;
                        task.container = hole.container == 0 ? QStringLiteral("tube") : QStringLiteral("cup");
                        task.sample = sample;
                        task.presetSampleId = hole.sampleId;
                        task.project = project;
                        for (const auto &unit : units_) task.unitRatios.insert(unit.name, QString::number(unit.ratio,'g',17));
                        task.rate = preset.rate;
                        task.prolonged = preset.prolonged;
                        if (!reagents_.assignTask(reagents, &task, remaining, message)) return false;
                        QStringList fields;
                        if (!taskFields(task, linuxNew_, &fields, message)) return false;
                        result.append(task);
                        if (result.size() > 4096) return fail(message, QStringLiteral("本架任务超过4096条，未截断下发。"));
                    }
                }
    }
    *tasks = result;
    return true;
}

bool OriginalDetectionSession::prepare(bool linuxNew, QString *message)
{
    prepared_ = false;
    if (!samples_) return fail(message, QStringLiteral("原样本数据未就绪。"));
    linuxNew_ = linuxNew;
    if (!samples_->load(&snapshot_, message)) return false;
    projects_ = samples_->projects();
    units_ = samples_->units();
    for (auto &project : projects_)
        if (!fullProjectActions(root_, &project, message)) return false;
    OriginalReagentState observed;
    if (!reagents_.load(&observed, message) || !reagents_.cleanerReady(observed, message)) return false;
    int count = 0;
    QList<Task> allTasks;
    QMap<qint64, double> remaining;
    for (const auto &rack : snapshot_.racks)
    {
        if (issuedRacks_.contains(rack.id))
        {
            bool finished = true, newTask = false;
            for (const auto &task : issuedTasks_) if (task.rack == rack.name && !finishedTasks_.contains(task.taskId)) finished = false;
            for (const auto &hole : rack.holes)
                if (hole.revise && sampleHoleState(sampleHoleTaskStates(hole)) == SampleHoleState::Ready) newTask = true;
            if (finished && newTask) issuedRacks_.remove(rack.id);
        }
        if (issuedRacks_.contains(rack.id)) continue;
        QList<Task> tasks;
        if (!tasksForRack(rack, nullptr, observed, &tasks, &remaining, message)) return false;
        count += tasks.size();
        allTasks.append(tasks);
    }
    bool unfinished = false;
    for (const auto &task : issuedTasks_) if (!finishedTasks_.contains(task.taskId)) unfinished = true;
    if (!count && !unfinished) return fail(message, QStringLiteral("未设置可下发的新任务，无法开始检测。"));
    if (!reagents_.auxiliaryVolumesReady(observed, allTasks, message) || !calibrationGroupsReady(root_, observed, allTasks, message)) return false;
    prepared_ = true;
    return true;
}

void OriginalDetectionSession::cancelUnsent()
{
    // 仅取消尚未分配设备任务号的开始准备；已占号任务不删除、不恢复试剂量。
    prepared_ = false;
}

bool OriginalDetectionSession::reserve(const OriginalReagentState &observed, QList<Task> *tasks,
                                      const QMap<qint64, double> &remaining, QString *message)
{
    if (!tasks || tasks->isEmpty()) return true;
    if (!versionMatches(observed, message)) return false;
    Database database;
    if (!database.open(root_, QStringLiteral("result/results.db"), true, message)) return false;
    const QString setting = QDir(root_).filePath(QStringLiteral("db/userSet/reagent_Setting.db"));
    const QString settingUri=QUrl::fromLocalFile(setting).toString(QUrl::FullyEncoded)+QStringLiteral("?mode=rw");
    if (!database.execute(QStringLiteral("ATTACH DATABASE ? AS original_reagents"), {settingUri}, message)) return false;
    // 原编号、结果占位与剩余量由同一SQLite连接提交；不改任何原表结构或持久化模式。
    for (const auto &schema : {QStringLiteral("main"), QStringLiteral("original_reagents")})
    {
        QSqlQuery query(database.db);
        if (!query.exec(QStringLiteral("PRAGMA %1.journal_mode").arg(schema)) || !query.next())
            return fail(message, QStringLiteral("无法核对原数据库事务模式。"));
        const QString mode = query.value(0).toString().toLower();
        if (mode != QStringLiteral("delete") && mode != QStringLiteral("truncate") && mode != QStringLiteral("persist"))
            return fail(message, QStringLiteral("原任务与试剂库当前事务模式不能保证一起恢复，未更改模式，请先完成该模式的兼容验证。"));
    }
    if (!database.begin(message) || !versionMatches(observed, message)) return false;
    QSqlQuery counters(database.db);
    if (!counters.exec(QStringLiteral("SELECT total_count,row_total_count FROM total_count WHERE id=1")))
        return fail(message, QStringLiteral("原results.db任务计数表无法读取：") + counters.lastError().text());
    qint64 nextTask = 1, nextRow = 1;
    const bool existed = counters.next();
    if (existed)
    {
        bool taskOk = false, rowOk = false;
        nextTask = counters.value(0).toLongLong(&taskOk);
        nextRow = counters.value(1).toLongLong(&rowOk);
        if (!taskOk || !rowOk || nextTask < 1 || nextRow < 1) return fail(message, QStringLiteral("原任务号或报告行计数损坏，未重置为1。"));
    }
    counters.finish();
    QSqlQuery maximum(database.db);
    if (!maximum.exec(QStringLiteral("SELECT COALESCE(MAX(task_id),0) FROM report_results")) || !maximum.next())
        return fail(message, QStringLiteral("无法核对原结果任务号，未分配新任务。"));
    if (maximum.value(0).toLongLong() >= nextTask) return fail(message, QStringLiteral("原任务计数未超过已有任务编号，禁止重复占号。"));
    maximum.finish();
    if (!maximum.exec(QStringLiteral("SELECT COALESCE(MAX(row_id),0) FROM report_data")) || !maximum.next())
        return fail(message, QStringLiteral("无法核对原报告行号。"));
    if (maximum.value(0).toLongLong() >= nextRow) return fail(message, QStringLiteral("原报告行计数未超过已有行号，禁止覆盖。"));
    maximum.finish();
    const qint64 oldTask = nextTask, oldRow = nextRow;
    QMap<qint64, qint64> rows;
    QList<Task> assigned = *tasks;
    const QString time = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddHHmmss"));
    for (auto &task : assigned)
    {
        if (nextTask >= std::numeric_limits<int>::max() || nextRow >= std::numeric_limits<int>::max())
            return fail(message, QStringLiteral("原任务号或报告行号已耗尽。"));
        const qint64 reportKey = task.reportRow;
        if (!rows.contains(reportKey))
        {
            rows[reportKey] = nextRow++;
            bool sampleNumeric = false;
            const qint64 sampleNumber = task.presetSampleId.toLongLong(&sampleNumeric);
            if (!database.execute(QStringLiteral("INSERT INTO report_data(row_id,sn,type,statue,uploaded,is_auto_id,sample_hole,sample_id,data_time,visible,state) VALUES(?,?,0,0,0,0,'',?,?,1,?)"),
                {rows[reportKey], sampleNumeric ? sampleNumber : 0, task.sample, time, QStringLiteral("待测试")}, message, 1)) return false;
        }
        task.reportRow = rows[reportKey];
        task.taskId = nextTask++;
        if (!database.execute(QStringLiteral("INSERT INTO report_results(fid,project_name,task_id,uploaded,calibration_id,abnormal_type,rate,sample_id,data_time,original_value,calibration_value,rate_value,inr_value,mda_value,formula_value) VALUES(?,?,?,0,'','',?,?,?,'','','','','','')"),
            {task.reportRow, task.project.name, task.taskId, task.rate,
             task.presetSampleId.isEmpty() ? task.sample : task.presetSampleId, time}, message, 1)) return false;
    }
    if (existed)
    {
        if (!database.execute(QStringLiteral("UPDATE total_count SET total_count=?,row_total_count=? WHERE id=1 AND total_count=? AND row_total_count=?"),
            {nextTask, nextRow, oldTask, oldRow}, message, 1)) return false;
    }
    else if (!database.execute(QStringLiteral("INSERT INTO total_count(id,total_count,project_total_count,row_total_count,cup_conut,cup_limit_count,auto_string,count) VALUES(1,?,1,?,1,1,1,1)"),
        {nextTask, nextRow}, message, 1)) return false;
    for (const auto &hole : observed.holes)
    {
        const qint64 id = hole.value(QStringLiteral("id")).toLongLong();
        if (!remaining.contains(id)) continue;
        bool ok = false;
        const double previous = hole.value(QStringLiteral("used_ul")).toString().toDouble(&ok);
        const double volume = remaining.value(id);
        if (!ok || !std::isfinite(volume) || volume < 0 || volume > previous) return fail(message, QStringLiteral("原试剂预留量非法。"));
        if (volume == previous) continue;
        if (!database.execute(QStringLiteral("UPDATE original_reagents.\"%1\" SET used_ul=? WHERE id=? AND used_ul=?").arg(observed.settingTable),
            {QString::number(volume, 'g', 15), id, hole.value(QStringLiteral("used_ul"))}, message, 1)) return false;
    }
    QByteArray validated;
    if (!rackPayload(assigned.first().rack, assigned, linuxNew_, &validated, message)) return false;
    if (!database.commit(message)) return false;
    *tasks = assigned;
    return true;
}

bool OriginalDetectionSession::answerRackRequest(const QByteArray &payload, QByteArray *response, QString *message)
{
    if (!prepared_ || !samples_ || !response) return fail(message, QStringLiteral("原样本任务尚未完成开始前准备，未回应设备任务请求。"));
    RackRequest request;
    if (!parseRackRequest(payload, &request, message)) return false;
    // 原版在设备索取时读取最新已保存的架任务，允许开始后为尚未下发的架补设任务。
    if (!samples_->load(&snapshot_, message)) return false;
    const SampleRack *selected = nullptr;
    for (const auto &rack : snapshot_.racks)
    {
        bool ready = false;
        for (const auto &hole : rack.holes)
            if (hole.revise && sampleHoleState(sampleHoleTaskStates(hole)) == SampleHoleState::Ready) ready = true;
        if (!ready || issuedRacks_.contains(rack.id)) continue;
        if (rack.name == request.rack || (rackScannerClosed_ && request.rack != QStringLiteral("Emergency") && !rack.emergency && !issuedRacks_.contains(rack.id)))
        { selected = &rack; break; }
    }
    if (!selected || issuedRacks_.contains(selected->id))
        return rackPayload(request.rack, {}, linuxNew_, response, message);
    for (const auto &project : projects_)
        if (project.originalFingerprint != OriginalProjectRepository::databaseFingerprint(QDir(root_).filePath(QStringLiteral("project/") + project.originalFileName), message))
            return fail(message, QStringLiteral("开始准备后的项目参数已变化，未发送旧参数，请核对并重新开始。"));
    OriginalReagentState observed;
    if (!reagents_.load(&observed, message)) return false;
    QList<Task> tasks;
    QMap<qint64, double> remaining;
    if (!tasksForRack(*selected, &request, observed, &tasks, &remaining, message)) return false;
    if (!reagents_.auxiliaryVolumesReady(observed, tasks, message) || !calibrationGroupsReady(root_, observed, tasks, message)) return false;
    if (!reserve(observed, &tasks, remaining, message)) return false;
    if (!rackPayload(selected->name, tasks, linuxNew_, response, message)) return false;
    if (!tasks.isEmpty())
    {
        issuedRacks_.insert(selected->id);
        issuedTasks_.append(tasks);
        samples_->setRackInFlight(selected->id, true);
    }
    return true;
}

bool OriginalDetectionSession::receiveDevicePayload(const QByteArray &payload, QString *message)
{
    if (payload.startsWith("Ur")) return reagents_.applyDeviceReply(payload, message);
    if (payload.startsWith("TD"))
    {
        ReceivedProgress progress;
        if (!parseProgress(payload, &progress, message)) return false;
        for (const auto &task : issuedTasks_)
            if (task.taskId == progress.taskId)
            {
                bool holeOk = false;
                const int hole = progress.hole.toInt(&holeOk);
                if (progress.rack != task.rack || !holeOk || hole != task.hole ||
                    (!progress.project.isEmpty() && progress.project != task.project.name))
                    return fail(message, QStringLiteral("设备加样进度的样本位置或项目与已下发任务不符，未改动其他样本。"));
                if (finishedTasks_.contains(task.taskId)) return true;
                return samples_->updateDeviceTask(task, SampleTaskState::Testing, message);
            }
        return fail(message, QStringLiteral("设备带回了本次会话未下发的进度任务号，未推测对应样本。"));
    }
    if (!payload.startsWith("Tr")) return fail(message, QStringLiteral("不是已接入的原设备数据消息。"));
    ReceivedResult result;
    if (!parseResult(payload, &result, message)) return false;
    const Task *known = nullptr;
    for (const auto &task : issuedTasks_) if (task.taskId == result.taskId) { known = &task; break; }
    if (!known) return fail(message, QStringLiteral("结果任务号不在本次已下发清单，未生成新样本或猜测归属。"));
    bool holeOk = false;
    const int hole = result.fields.value(QStringLiteral("sample_hole")).toInt(&holeOk);
    if (result.fields.value(QStringLiteral("rack_id")) != known->rack || !holeOk || hole != known->hole ||
        result.fields.value(QStringLiteral("project_name")) != known->project.name ||
        result.fields.value(QStringLiteral("sample_id")) != known->sample ||
        result.fields.value(QStringLiteral("taskType")) != known->type)
        return fail(message, QStringLiteral("结果的样本架、孔位、条码、类型或项目与原下发任务不符，未写到其他样本。"));
    if (!OriginalBusiness::workingRoot(root_, message)) return false;
    const QString day = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    const QString relative = QStringLiteral("result/") + day;
    const QString folder = QDir(root_).filePath(relative);
    for (const auto &path : {QDir(root_).filePath(QStringLiteral("result")), folder})
        if (QFileInfo(path).isSymLink() || (QFileInfo::exists(path) && !QFileInfo(path).isDir()))
            return fail(message, QStringLiteral("结果目录不是普通文件夹，或经过了符号链接。"));
    if (!QDir(root_).mkpath(relative)) return fail(message, QStringLiteral("无法创建原格式结果日期目录，原始结果未保存。"));
    const QString target = QDir(folder).filePath(QString::number(known->taskId));
    const QString digest = QString::fromLatin1(QCryptographicHash::hash(payload,QCryptographicHash::Sha256).toHex());
    OriginalIni::Settings previous;
    bool sameResult = false;
    if (QFileInfo::exists(target) || QFileInfo(target).isSymLink())
    {
        if (QFileInfo(target).isSymLink() || !OriginalIni::read(target,&previous,message)) return false;
        const QString oldDigest = previous.values.value(QStringLiteral("rk/raw_payload_sha256")).toString();
        sameResult = oldDigest == digest;
        if ((!previous.values.value(QStringLiteral("base/task_id")).toString().isEmpty() &&
             previous.values.value(QStringLiteral("base/task_id")).toLongLong() != known->taskId) ||
            (!previous.values.value(QStringLiteral("base/project_name")).toString().isEmpty() &&
             previous.values.value(QStringLiteral("base/project_name")).toString() != known->project.name))
            return fail(message,QStringLiteral("已有结果文件的任务或项目身份不符，未覆盖。"));
        if ((!oldDigest.isEmpty() && !sameResult) ||
            (!sameResult && !previous.values.value(QStringLiteral("result/original")).toString().isEmpty()))
            return fail(message, QStringLiteral("同一任务号已有不同的原始或计算结果，保留原文件，请核对重复返回。"));
        if (sameResult && !previous.values.value(QStringLiteral("result/original")).toString().isEmpty()) return true;
    }
    if (!sameResult)
    {
        QTemporaryFile temporary(QDir(folder).filePath(QStringLiteral(".sn100-result-XXXXXX")));
        if (!temporary.open()) return fail(message, QStringLiteral("无法准备原始结果文件：") + temporary.errorString());
        const QString temporaryName = temporary.fileName();
        temporary.close();
        {
            QSettings settings(temporaryName,QSettings::IniFormat);
            settings.setIniCodec("UTF-8");
            settings.setFallbacksEnabled(false);
            for (auto i = previous.values.cbegin(); i != previous.values.cend(); ++i) settings.setValue(i.key(),i.value());
            for (const auto &key : {QStringLiteral("taskType"),QStringLiteral("sample_id"),QStringLiteral("task_id"),QStringLiteral("project_id"),QStringLiteral("project_name")})
                settings.setValue(QStringLiteral("base/") + key,result.fields.value(key));
            settings.setValue(QStringLiteral("base/row_id"),known->reportRow);
            settings.setValue(QStringLiteral("base/date_time"),QDateTime::currentDateTime().toString(QStringLiteral("yyyy/MM/dd HH:mm:ss")));
            for (const auto &key : {QStringLiteral("warm_channel"),QStringLiteral("test_channel"),QStringLiteral("needle_num"),QStringLiteral("needle_temp"),
                 QStringLiteral("reagent_holeName"),QStringLiteral("sample_rate"),QStringLiteral("sample_frequency"),QStringLiteral("rack_id"),
                 QStringLiteral("sample_hole"),QStringLiteral("batch_group"),QStringLiteral("IBS_holeName")})
                settings.setValue(QStringLiteral("test/") + key,result.fields.value(key));
            for (auto i = result.fields.cbegin(); i != result.fields.cend(); ++i)
                if (i.key().startsWith(QStringLiteral("spectrum_"))) settings.setValue(QStringLiteral("spectrum/") + i.key().mid(9),i.value());
            // 仅保存实际原始数据；缺算法时不能写0或空成功结果充当计算值。
            settings.setValue(QStringLiteral("rk/raw_payload_sha256"),digest);
            settings.setValue(QStringLiteral("rk/raw_payload_hex"),QString::fromLatin1(payload.toHex()));
            settings.setValue(QStringLiteral("rk/calculation_state"),QStringLiteral("waiting"));
            settings.setValue(QStringLiteral("rk/preset_sample_id"),known->presetSampleId);
            for (auto i = known->project.originalBaseValues.cbegin(); i != known->project.originalBaseValues.cend(); ++i)
                settings.setValue(QStringLiteral("rk_project_base/") + i.key(),i.value());
            for (auto i = known->project.originalAlgorithmValues.cbegin(); i != known->project.originalAlgorithmValues.cend(); ++i)
                settings.setValue(QStringLiteral("rk_algorithm/") + i.key(),i.value());
            for (auto i = known->unitRatios.cbegin(); i != known->unitRatios.cend(); ++i)
                settings.setValue(QStringLiteral("rk_unit_ratios/") + i.key(),i.value());
            for (const auto &resultSetting : known->project.results)
            {
                const QString group = QStringLiteral("rk_results/") + resultSetting.type;
                settings.setValue(group + QStringLiteral("/unit"),resultSetting.unit);
                settings.setValue(group + QStringLiteral("/decimals"),resultSetting.decimals);
                settings.setValue(group + QStringLiteral("/used"),resultSetting.used);
                settings.setValue(group + QStringLiteral("/main"),resultSetting.mainResult);
            }
            settings.sync();
            if (settings.status() != QSettings::NoError) return fail(message, QStringLiteral("原始结果临时文件保存失败，未替换目标。"));
        }
        QFile source(temporaryName);
        if (!source.open(QIODevice::ReadOnly)) return fail(message,source.errorString());
        const QByteArray content = source.readAll();
        if (source.error() != QFile::NoError) return fail(message,source.errorString());
        QSaveFile output(target);
        if (!output.open(QIODevice::WriteOnly) || output.write(content) != content.size() || !output.commit())
            return fail(message,QStringLiteral("原始结果未能原子保存：") + output.errorString());
    }
    Database database;
    if (!database.open(root_,QStringLiteral("result/results.db"),true,message) || !database.begin(message)) return false;
    if (!database.execute(QStringLiteral("UPDATE report_data SET state=? WHERE row_id=? AND sample_id=? AND EXISTS(SELECT 1 FROM report_results WHERE task_id=? AND fid=report_data.row_id AND project_name=?)"),
        {QStringLiteral("已收到曲线，待计算"),known->reportRow,known->sample,known->taskId,known->project.name},message,1) || !database.commit(message)) return false;
    if (!samples_->updateDeviceTask(*known,SampleTaskState::Finished,message)) return false;
    finishedTasks_.insert(known->taskId);
    bool rackFinished = true;
    for (const auto &task : issuedTasks_) if (task.rack == known->rack && !finishedTasks_.contains(task.taskId)) rackFinished = false;
    if (rackFinished)
        for (const auto &rack : samples_->snapshot().racks) if (rack.name == known->rack) samples_->setRackInFlight(rack.id,false);
    return true;
}

bool OriginalDetectionSession::reagentConfiguration(QByteArray *payload, QString *message)
{
    OriginalReagentState state;
    return reagents_.load(&state,message) && reagents_.reagentUpdatePayload(state,payload,message);
}
