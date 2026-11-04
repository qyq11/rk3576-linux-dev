#include "original_detection_session.h"
#include "original_task_reagent_receipt_writer.h"
#include "projects/original_project_copy.h"
#include <QLockFile>
#include "original_control_tasks.h"
#include "original_files/original_ini.h"
#include "original_result_codec.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
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
using OriginalReagentAllocation::AllocationResult;
using OriginalReagentAllocation::AmountWork;
using OriginalReagentAllocation::Mode;
using OriginalReagentAllocation::SelectionContext;
using OriginalReagentAllocation::SkipScope;
using OriginalReagentAllocation::Status;
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
        if (task.type != QStringLiteral("CAL") && task.project.originalBaseValues.value(QStringLiteral("is_related")).toInt() > 0) projects.insert(task.project.name);
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

void recordAllocation(const Task &task, const AllocationResult &allocation)
{
    if (allocation.status!=Status::Selected || !allocation.limitations.isEmpty())
        qInfo().noquote()<<"TASK_REAGENT_ALLOCATION"<<allocation.scopeId<<int(allocation.status)
            <<int(allocation.skipScope)<<task.rack<<task.hole<<task.project.name<<task.repeatIndex<<allocation.reason;
    for (const auto &limitation : allocation.limitations)
        qInfo().noquote()<<"TASK_REAGENT_ALLOCATION_LIMIT"<<allocation.scopeId<<task.rack
            <<task.hole<<task.project.name<<task.repeatIndex<<limitation;
}

bool reservationWorkMatches(const OriginalReagentState &observed,const QList<Task> &tasks,
                            const AmountWork &work,QString *message)
{
    if (!work.initialized || work.root!=observed.root || work.versions!=observed.versions)
        return fail(message,QStringLiteral("任务逻辑余量不属于本次原资料和版本，未保存旧分配结果。"));
    QSet<qint64> touched;
    for (const auto &task : tasks)
        for (auto entry=task.reservedReagentVolumes.cbegin();entry!=task.reservedReagentVolumes.cend();++entry)
        {
            if (entry.key()<0 || !std::isfinite(entry.value()) || entry.value()<0)
                return fail(message,QStringLiteral("任务原配置量账本的孔或数值无效，未以余额净差补账。"));
            touched.insert(entry.key());
        }
    QSet<qint64> textHoles;
    for (auto text=work.finalUsedText.cbegin();text!=work.finalUsedText.cend();++text) textHoles.insert(text.key());
    if (touched!=work.touched || touched!=textHoles)
        return fail(message,QStringLiteral("任务实际触及孔、配置量账本和末次保存文本不一致，未改写未选孔。"));
    for (const auto id : touched)
    {
        const OriginalBusiness::Row *row=nullptr;
        for (const auto &candidate : observed.holes)
            if (candidate.value(QStringLiteral("id")).toLongLong()==id)
            {
                if (row) return fail(message,QStringLiteral("任务触及的原孔编号不唯一，未保存。"));
                row=&candidate;
            }
        if (!row || !work.initialUsed.contains(id) || !work.remaining.contains(id))
            return fail(message,QStringLiteral("任务触及孔缺少真实原行、初值或逻辑写后值。"));
        const QVariant original=row->value(QStringLiteral("used_ul"));
        const QVariant initial=work.initialUsed.value(id);
        if (initial.userType()!=original.userType() || initial.isNull()!=original.isNull() || initial!=original)
            return fail(message,QStringLiteral("任务逻辑初值不同于本次原WHERE值，未格式化旧值来绕过版本核对。"));
        bool beforeOk=false,textOk=false;
        const double previous=original.toString().toDouble(&beforeOk);
        const QString text=work.finalUsedText.value(id);
        const double amount=text.toDouble(&textOk);
        if (!beforeOk || !std::isfinite(previous) || previous<0 || text.isEmpty() || !textOk ||
            !std::isfinite(amount) || amount<0 || !std::isfinite(work.remaining.value(id)) || amount!=work.remaining.value(id))
            return fail(message,QStringLiteral("任务原量或末次逻辑保存文本不是对应的有限非负值。"));
        // 已核步骤在分配器内按原下界及g6更新；末次文本可因舍入略增，不能再按净差拒绝。
    }
    return true;
}

bool refundTexts(const OriginalReagentState &observed,const QList<Task> &pending,
                 const QSet<qint64> &added,const QMap<qint64,OriginalBusiness::Rows> &reservedRows,
                 QMap<qint64,QString> *finalUsedText,QString *message)
{
    QMap<qint64,double> working;
    QMap<qint64,QString> texts;
    for (const auto &task : pending)
    {
        if (added.contains(task.taskId)) continue; // 原TD8整任务标记，不推断物理逐步已加量。
        QMap<QString,QPair<qint64,double>> taskHoles;
        for (auto entry=task.reservedReagentVolumes.cbegin();entry!=task.reservedReagentVolumes.cend();++entry)
        {
            const OriginalBusiness::Row *before=nullptr,*current=nullptr;
            const auto reserved=reservedRows.constFind(task.taskId);
            if (reserved!=reservedRows.cend())
                for (const auto &row : reserved.value())
                    if (row.value(QStringLiteral("id")).toLongLong()==entry.key())
                    {
                        if (before) return fail(message,QStringLiteral("任务原配置量账对应的保存孔编号重复。"));
                        before=&row;
                    }
            for (const auto &row : observed.holes)
                if (row.value(QStringLiteral("id")).toLongLong()==entry.key())
                {
                    if (current) return fail(message,QStringLiteral("退量当前孔编号重复，未合并原行。"));
                    current=&row;
                }
            if (!before || !current || !std::isfinite(entry.value()) || entry.value()<0)
                return fail(message,QStringLiteral("缺少任务原配置量账、原孔或有限非负量，未推算退量。"));
            for (const auto &key : {"hole_name","reagent_name","reagent_type","reagent_batch","bottle_type","datetime","area"})
                if (before->value(QLatin1String(key))!=current->value(QLatin1String(key)))
                    return fail(message,QStringLiteral("任务下发后试剂瓶资料变化，未向其他瓶退量。"));
            const QString name=current->value(QStringLiteral("hole_name")).toString();
            if (name.isEmpty() || taskHoles.contains(name))
                return fail(message,QStringLiteral("任务退量孔名为空或不唯一，未按编号猜原孔顺序。"));
            taskHoles.insert(name,qMakePair(entry.key(),entry.value()));
            if (!working.contains(entry.key()))
            {
                bool ok=false;
                const double previous=current->value(QStringLiteral("used_ul")).toDouble(&ok);
                if (!ok || !std::isfinite(previous))
                    return fail(message,QStringLiteral("退量前的原试剂余量不是有限数，未用默认0补齐。"));
                working.insert(entry.key(),previous);
            }
        }
        // 原getRgInSingleTesting先在本测试汇总同孔配置量，restore再按孔名QMap顺序写g6。
        for (auto hole=taskHoles.cbegin();hole!=taskHoles.cend();++hole)
        {
            const qint64 id=hole->first;
            const double raw=working.value(id)+hole->second;
            if (!std::isfinite(raw)) return fail(message,QStringLiteral("任务逐次退量溢出，未截断或夹到瓶上限。"));
            const QString text=QString::number(raw>0.0 ? raw : 0.0);
            bool ok=false;
            const double rounded=text.toDouble(&ok);
            if (!ok || !std::isfinite(rounded) || rounded<0)
                return fail(message,QStringLiteral("任务退量g6文本无法对应有限非负读回值。"));
            working[id]=rounded;
            texts[id]=text; // 选中的0配置量仍保留写入意图；同孔后一个任务承接本次g6读值。
        }
    }
    *finalUsedText=texts;
    return true;
}
}

bool OriginalDetectionSession::initialize(OriginalSampleRepository *samples, QString *message,
    std::shared_ptr<OriginalReagentPageMetadata> metadata,
    OriginalIni::InitializationSourcePtr configurationSource)
{
    initializedConfigurationVersions_.clear();
    configurationSource_.reset();
    if (!samples || samples->root().isEmpty()) return fail(message, QStringLiteral("原样本仓库未就绪。"));
    // 旧接口只可复用仓库真正消费的同一来源，不能在仓库读完后另抓新版给旧缓存续签。
    if (!configurationSource) configurationSource=samples->configurationSource();
    if (configurationSource && (configurationSource!=samples->configurationSource() ||
        !configurationSource->current(samples->root(),message)))
        return fail(message,QStringLiteral("样本仓库和任务会话的初始化配置来源不一致，未采用任务缓存。"));
    samples_ = samples;
    pageMetadata_ = std::move(metadata);
    reagents_.setPageMetadata(pageMetadata_);
    root_ = samples->root();
    if (!reagents_.initialize(root_, message, configurationSource)) return false;
    OriginalIni::Settings settings, debug, theme;
    if (configurationSource)
    {
        if (reagents_.configurationSource()!=configurationSource)
            return fail(message,QStringLiteral("内置试剂仓库未消费同一初始化配置，未采用任务缓存。"));
        settings=configurationSource->normal(); debug=configurationSource->debug(); theme=configurationSource->theme();
    }
    else if (!OriginalIni::read(QDir(root_).filePath(QStringLiteral("config.ini")), &settings, message) ||
        !OriginalIni::read(QDir(root_).filePath(QStringLiteral("config_Debug.ini")), &debug, message) ||
        !OriginalIni::read(QDir(root_).filePath(QStringLiteral("theme_config.ini")), &theme, message)) return false;
    if (!boolean(settings, QStringLiteral("userOption/isSampleHoleScanClose"), false, &scannerClosed_, message) ||
        !boolean(settings, QStringLiteral("userOption/isSampleScanClose"), false, &rackScannerClosed_, message) ||
        !boolean(settings, QStringLiteral("userOption/isSampleHoleScanOpenHoleNoneWork"), true, &allowMissingBarcode_, message) ||
        !boolean(debug, QStringLiteral("userOption/SendTaskByRowID"), true, &sendByReportRow_, message) ||
        !boolean(debug, QStringLiteral("userOption/TaskSendDelete"), false, &deleteSentTasks_, message)) return false;
    const QString report = theme.values.value(QStringLiteral("theme/ReportTableType"), QStringLiteral("0")).toString();
    if (report != QStringLiteral("0") && report != QStringLiteral("1")) return fail(message, QStringLiteral("原报告排版配置不是0或1。"));
    horizontalReport_ = report == QStringLiteral("0");
    // 原main.cpp:163/181从同一config.ini取QStringList；不新增键或人工拆分逗号。
    multiBottleExceptions_=settings.values.value(QStringLiteral("Reagent/exMultil"),QVariant()).toStringList();
    if (configurationSource)
    {
        if (!configurationSource->current(root_,message)) return false;
        initializedConfigurationVersions_=configurationSource->versions();
        configurationSource_=std::move(configurationSource);
    }
    return true;
}

bool OriginalDetectionSession::capturePageMetadata(const OriginalReagentState &state, AmountWork *work, QString *message) const
{
    return !pageMetadata_ || pageMetadata_->capture(state.root,state.holes,&work->pageMetadata,message);
}

bool OriginalDetectionSession::tasksForRack(const SampleRack &rack, const RackRequest *request,
                                          const OriginalReagentState &reagents, QList<Task> *tasks,
                                          AmountWork *work, QString *message) const
{
    if (!tasks || !work) return fail(message,QStringLiteral("原任务列表或逻辑余量输出为空。"));
    QList<Task> result;
    // 原slotSendRackTasks先按项目发送CAL，再发送普通/质控；CAL不经普通条码筛选。
    for (const auto &project : projects_)
    {
        SelectionContext selection;
        bool skipProject=false;
        for (const auto &hole : rack.holes)
        {
            if (skipProject) break;
            if (hole.sampleType != 1 || !hole.revise || sampleHoleState(sampleHoleTaskStates(hole)) != SampleHoleState::Ready) continue;
            if (!samples_->validateControlHole(hole, rack.holes.size(), message)) return false;
            for (const auto &preset : hole.tasks)
            {
                if (preset.projectId != project.id) continue;
                const auto states = sampleTaskStates(preset);
                for (int repeat = 0; repeat < preset.repeats; ++repeat)
                {
                    if (repeat >= states.size() || (states[repeat] != SampleTaskState::Idle && states[repeat] != SampleTaskState::Reset)) continue;
                    Task task;
                    task.type = QStringLiteral("CAL"); task.taskId = 1; task.calibrationId = 1;
                    task.rack = rack.name; task.hole = hole.number; task.repeatIndex = repeat;
                    task.container = hole.container == 0 ? QStringLiteral("tube") : QStringLiteral("cup");
                    // 原手动链未把Tt条码赋给CAL的sampleCode；其初值为空。
                    // c-批号是sampleID，不能伪作扫码所得的sampleCode。LIS校准链另待接入。
                    task.sample = QString(); task.presetSampleId = hole.sampleId;
                    task.project = project; task.rate = preset.rate;
                    task.calibration = hole.calibration; task.calibrationPoint = preset.calibrationPoint;
                    task.calibrationConcentration = preset.calibrationConcentration;
                    for (const auto &unit : units_) task.unitRatios.insert(unit.name, QString::number(unit.ratio, 'g', 17));
                    if (!selection.initialized && !reagents_.beginSelectionScope(reagents,projects_,QStringList{project.name},
                        Mode::Calibration,linuxNew_,QStringLiteral("%1/cal:%2:%3").arg(rack.name).arg(project.id).arg(project.originalFileName),
                        multiBottleExceptions_,*work,&selection,message)) return false;
                    const auto allocation=OriginalControlTasks::assignCalibration(reagents_,reagents,linuxNew_,&task,&selection,work);
                    recordAllocation(task,allocation);
                    if (allocation.status==Status::Fatal)
                        return fail(message,allocation.reason.isEmpty() ? QStringLiteral("原校准试剂分配失败。") : allocation.reason);
                    if (allocation.status==Status::OriginalSkipped)
                    {
                        if (allocation.skipScope==SkipScope::Project) { skipProject=true; break; }
                        if (allocation.skipScope==SkipScope::Test) continue;
                        return fail(message,QStringLiteral("原校准跳过结果未给出明确范围，未编码空任务。"));
                    }
                    task.reservedReagentVolumes=allocation.configuredVolumes;
                    QStringList fields;
                    if (!taskFields(task, linuxNew_, &fields, message)) return false;
                    result.append(task);
                    if (result.size() > 4096) return fail(message, QStringLiteral("本架校准任务超过4096条，未截断。"));
                }
                if (skipProject) break;
            }
        }
    }
    // 原sendTaskByRowID先遍历同一报告行的重复次数，再按项目表顺序整理项目。
    int repetitions = 1;
    if (sendByReportRow_)
        for (const auto &hole : rack.holes)
            if (hole.sampleType != 1) for (const auto &task : hole.tasks) repetitions = qMax(repetitions, task.repeats);
    for (const auto &hole : rack.holes)
    {
        if (hole.sampleType == 1 || !hole.revise || sampleHoleState(sampleHoleTaskStates(hole)) != SampleHoleState::Ready) continue;
        if (!samples_->validateControlHole(hole, rack.holes.size(), message)) return false;
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
        SelectionContext selection;
        if (!reagents_.beginSelectionScope(reagents,projects_,QStringList(),Mode::Normal,linuxNew_,
            QStringLiteral("%1/sample:%2").arg(rack.name).arg(hole.number),multiBottleExceptions_,*work,&selection,message)) return false;
        QSet<QString> skippedProjects;
        for (int round = 0; round < repetitions; ++round)
            for (const auto &project : projects_)
            {
                if (skippedProjects.contains(project.name)) continue;
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
                        if (hole.sampleType == 2)
                        {
                            task.type = QStringLiteral("QC"); task.quality = hole.quality.selected;
                            for (const auto &ref : task.quality.projects)
                                if (ref.projectId == project.id) task.qualityAssociationId = ref.associationId;
                            if (task.qualityAssociationId <= 0) return fail(message, QStringLiteral("原质控任务没有该项目的关联编号，未下发。"));
                        }
                        for (const auto &unit : units_) task.unitRatios.insert(unit.name, QString::number(unit.ratio,'g',17));
                        task.rate = preset.rate;
                        task.prolonged = preset.prolonged;
                        // SampleTask目前无原多瓶标记，页面多瓶组合尚未接；不按QC类型补true。
                        const auto allocation=reagents_.assignTask(reagents,&task,&selection,work);
                        recordAllocation(task,allocation);
                        if (allocation.status==Status::Fatal)
                            return fail(message,allocation.reason.isEmpty() ? QStringLiteral("原试剂分配失败。") : allocation.reason);
                        if (allocation.status==Status::OriginalSkipped)
                        {
                            if (allocation.skipScope==SkipScope::Project) { skippedProjects.insert(project.name); break; }
                            if (allocation.skipScope==SkipScope::Test) continue;
                            return fail(message,QStringLiteral("原试剂跳过结果未给出明确范围，未编码空任务。"));
                        }
                        task.reservedReagentVolumes=allocation.configuredVolumes;
                        QStringList fields;
                        if (!taskFields(task, linuxNew_, &fields, message)) return false;
                        result.append(task);
                        if (result.size() > 4096) return fail(message, QStringLiteral("本架任务超过4096条，未截断下发。"));
                    }
                    if (skippedProjects.contains(project.name)) break;
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
    if (deleteSentTasks_) return fail(message,QStringLiteral("当前INI启用了TaskSendDelete（下发后删除预设），该分支的报告进度和错误恢复尚待接入；未准备或发送新任务。"));
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
    AmountWork work;
    if (!reagents_.initializeAmountWork(observed,&work,message) || !capturePageMetadata(observed,&work,message)) return false;
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
        if (!tasksForRack(rack, nullptr, observed, &tasks, &work, message)) return false;
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

bool OriginalDetectionSession::prepareUserOperation(const OriginalTaskReagents::RuntimeBasis &basis,bool linuxNew,QString *message)
{
    cancelUnsent(); // 只撤旧的未占号准备，不清已发任务或退款依据。
    const auto matches=[&]()
    {
        if (!OriginalTaskReagents::taskOriginAllows(basis,OriginalTaskReagents::Action::None) ||
            basis.source!=OriginalTaskReagents::OperationSource::UserOperation || !samples_ || basis.root!=root_ ||
            !configurationSource_ || samples_->configurationSource()!=configurationSource_ ||
            reagents_.configurationSource()!=configurationSource_ || initializedConfigurationVersions_.size()!=3 ||
            basis.userOperation.configurationVersions!=initializedConfigurationVersions_ ||
            !configurationSource_->current(root_,message)) return false;
        SampleSnapshot current;
        if (!samples_->load(&current,message) || OriginalTaskReagents::sampleTaskInputDigest(current)!=basis.userOperation.sampleSnapshotDigest) return false;
        QMap<QString,QString> projects;
        for (const auto &project : samples_->projects())
        {
            if (project.originalFileName.isEmpty() || project.originalFingerprint.isEmpty() || projects.contains(project.originalFileName)) return false;
            projects.insert(project.originalFileName,project.originalFingerprint);
        }
        return projects==basis.userOperation.projectVersions;
    };
    if (!matches()) return fail(message,QStringLiteral("用户准备请求的来源、原配置或项目版本已改变，未准备任务。"));
    bool immediate=false;
    if (!boolean(configurationSource_->debug(),QStringLiteral("userOption/SendTaskImmediate"),false,&immediate,message)) return false;
    if (immediate) return fail(message,QStringLiteral("原配置启用了立即下发；Emergency业务层尚未接通，未改走Ts开始命令。"));
    const bool success=prepare(linuxNew,message);
    if (!matches())
    { cancelUnsent(); return fail(message,QStringLiteral("准备期间原配置或项目版本改变，旧用户请求已停止，未发送开始。")); }
    return success;
}

void OriginalDetectionSession::cancelUnsent()
{
    // 仅取消尚未分配设备任务号的开始准备；已占号任务不删除、不恢复试剂量。
    prepared_ = false;
}

bool OriginalDetectionSession::reserve(const OriginalReagentState &observed, QList<Task> *tasks,
                                      const AmountWork &work, QString *message, const QByteArray &request,
                                      const OriginalTaskReagents::RuntimeBasis &basis, OriginalTaskReagents::Commit *result,
                                      OriginalTaskReagents::CommitHandler committed)
{
    QLockFile projectLock(QDir(root_).filePath(QStringLiteral(".sn100-original-project-write.lock")));
    QLockFile reagentLock(QDir(root_).filePath(QStringLiteral(".sn100-original-reagent-write.lock")));
    projectLock.setStaleLockTime(0); reagentLock.setStaleLockTime(0);
    OriginalTaskReagents::ReceiptWriter receipt(OriginalTaskReagents::Action::RackReservation,root_,request,basis,result,committed);
    if (!tasks) return fail(message,QStringLiteral("原任务保存列表为空。"));
    if (tasks->isEmpty())
        return (work.touched.isEmpty() && work.finalUsedText.isEmpty()) ||
            fail(message,QStringLiteral("空任务仍有逻辑扣量，未保存无法归属的余额。"));
    if (!projectLock.tryLock(0) || !reagentLock.tryLock(0))
        return fail(message,QStringLiteral("原项目或试剂正在保存，任务尚未预留，未自动重发，请保留原请求核对。"));
    if (!OriginalProjectCopy::noPending(root_,message) || !versionMatches(observed, message) ||
        (pageMetadata_ && !pageMetadata_->current(work.pageMetadata,message))) return false;
    if (!reservationWorkMatches(observed,*tasks,work,message)) return false;
    QList<Task> assigned;
    {
        Database database;
        if (!database.open(root_, QStringLiteral("result/results.db"), true, message)) return false;
        const QString setting = QDir(root_).filePath(QStringLiteral("db/userSet/reagent_Setting.db"));
        const QString settingUri=QUrl::fromLocalFile(setting).toString(QUrl::FullyEncoded)+QStringLiteral("?mode=rw");
        if (!database.execute(QStringLiteral("ATTACH DATABASE ? AS original_reagents"), {settingUri}, message)) return false;
        bool hasCalibration = false;
        for (const auto &task : *tasks) if (task.type == QStringLiteral("CAL")) hasCalibration = true;
        if (hasCalibration && !OriginalControlTasks::attachCalibration(database, root_, message)) return false;
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
        if (!database.begin(message)) return false;
        receipt.transactionStarted();
        if (!versionMatches(observed, message)) return false;
        receipt.captureBefore(database.db,observed); // 只核显示凭据，缺证据不新增任务权限。
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
        assigned = *tasks;
        const QString time = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddHHmmss"));
        const QString issuedDay = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
        for (auto &task : assigned)
        {
            if (nextTask >= std::numeric_limits<int>::max() || nextRow >= std::numeric_limits<int>::max())
                return fail(message, QStringLiteral("原任务号或报告行号已耗尽。"));
            task.issuedDay = issuedDay;
            if (task.type == QStringLiteral("CAL"))
            { task.reportRow = 0; task.taskId = nextTask++; continue; }
            const qint64 reportKey = task.reportRow;
            if (!rows.contains(reportKey))
            {
                rows[reportKey] = nextRow++;
                bool sampleNumeric = false;
                const qint64 sampleNumber = task.presetSampleId.toLongLong(&sampleNumeric);
                if (!database.execute(QStringLiteral("INSERT INTO report_data(row_id,sn,type,statue,uploaded,is_auto_id,sample_hole,sample_id,data_time,visible,state) VALUES(?,?,?,0,0,0,'',?,?,1,?)"),
                    {rows[reportKey], sampleNumeric ? sampleNumber : 0, task.type == QStringLiteral("QC") ? 1 : 0, task.sample, time, QStringLiteral("待测试")}, message, 1)) return false;
            }
            task.reportRow = rows[reportKey];
            task.taskId = nextTask++;
            if (!database.execute(QStringLiteral("INSERT INTO report_results(fid,project_name,task_id,uploaded,calibration_id,abnormal_type,rate,sample_id,data_time,original_value,calibration_value,rate_value,inr_value,mda_value,formula_value) VALUES(?,?,?,0,'','',?,?,?,'','','','','','')"),
                {task.reportRow, task.project.name, task.taskId, task.rate,
                 task.presetSampleId.isEmpty() ? task.sample : task.presetSampleId, time}, message, 1)) return false;
        }
        if (hasCalibration && !OriginalControlTasks::reserveCalibration(database, &assigned, message)) return false;
        for (const auto &task : assigned) receipt.data().tasks.append(OriginalTaskReagents::taskOrigin(task));
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
            if (!work.touched.contains(id)) continue;
            // 包含已选0；直接绑定最后逻辑g6文本，不二次格式化或因数值相等跳过。
            const QString usedText=work.finalUsedText.value(id);
            if (!database.execute(QStringLiteral("UPDATE original_reagents.\"%1\" SET used_ul=? WHERE id=? AND used_ul=?").arg(observed.settingTable),
                {usedText, id, hole.value(QStringLiteral("used_ul"))}, message, 1)) return false;
            receipt.matchedUsedWrite(hole,usedText);
        }
        QByteArray validated;
        if (!rackPayload(assigned.first().rack, assigned, linuxNew_, &validated, message)) return false;
        receipt.captureTransactionAfter(database.db); // 真实亲和转换/typeof先在原事务内取证。
        if (!database.commit(message)) { receipt.commitFailed(); return false; }
        receipt.commitSeen();
    } // writer关闭；project/reagent两锁仍持有。
    receipt.finish();
    for (const auto &task : assigned)
        for (const auto &row : observed.holes)
            if (task.reservedReagentVolumes.contains(row.value(QStringLiteral("id")).toLongLong()))
                reservedReagentRows_[task.taskId].append(row);
    *tasks = assigned;
    return true;
}

bool OriginalDetectionSession::answerRackRequest(const QByteArray &payload, QByteArray *response, QString *message,
    const OriginalTaskReagents::RuntimeBasis &basis, OriginalTaskReagents::Commit *receipt,
    OriginalTaskReagents::CommitHandler committed)
{
    if (receipt) *receipt=OriginalTaskReagents::Commit();
    if (basis.source!=OriginalTaskReagents::OperationSource::DevicePacket ||
        (basis.actionId && (!OriginalTaskReagents::validTaskOrigin(basis) ||
            basis.payloadSha256!=QString::fromLatin1(QCryptographicHash::hash(payload,QCryptographicHash::Sha256).toHex()))))
        return fail(message,QStringLiteral("设备入口只接受原设备包来源；用户请求不得借伪Tt/TR进入事务。"));
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
    QList<Task> tasks;
    AmountWork work;
    {
        // 与Ur/手工覆盖串行到页面setter发布；释放后reserve再按原顺序取锁并复核。
        QLockFile projectLock(QDir(root_).filePath(QStringLiteral(".sn100-original-project-write.lock")));
        QLockFile reagentLock(QDir(root_).filePath(QStringLiteral(".sn100-original-reagent-write.lock")));
        projectLock.setStaleLockTime(0); reagentLock.setStaleLockTime(0);
        if (!projectLock.tryLock(0) || !reagentLock.tryLock(0))
            return fail(message,QStringLiteral("原项目或试剂正在保存，未采用旧盘面批号生成任务。"));
        if (!reagents_.load(&observed,message) || !reagents_.initializeAmountWork(observed,&work,message) ||
            !capturePageMetadata(observed,&work,message)) return false;
        // setter不属于SQL事务；未选中、随后跳过/失败也交回已经遍历的批号变化。
        const bool allocated = tasksForRack(*selected, &request, observed, &tasks, &work, message);
        QString metadataError;
        if (pageMetadata_ && !pageMetadata_->publishDefaults(&work.pageMetadata,&metadataError))
            return fail(message,metadataError);
        if (!allocated) return false;
    }
    if (!reagents_.auxiliaryVolumesReady(observed, tasks, message) || !calibrationGroupsReady(root_, observed, tasks, message)) return false;
    if (!reserve(observed, &tasks, work, message,payload,basis,receipt,committed)) return false;
    if (!rackPayload(selected->name, tasks, linuxNew_, response, message)) return false;
    if (!tasks.isEmpty())
    {
        issuedRacks_.insert(selected->id);
        issuedTasks_.append(tasks);
        samples_->setRackInFlight(selected->id, true);
    }
    return true;
}

bool OriginalDetectionSession::receiveDevicePayload(const QByteArray &payload, QString *message,
    const OriginalTaskReagents::RuntimeBasis &basis, OriginalTaskReagents::Commit *receipt,
    OriginalTaskReagents::CommitHandler committed)
{
    if (receipt) *receipt=OriginalTaskReagents::Commit();
    if (basis.source!=OriginalTaskReagents::OperationSource::DevicePacket ||
        (basis.actionId && (!OriginalTaskReagents::validTaskOrigin(basis) ||
            basis.payloadSha256!=QString::fromLatin1(QCryptographicHash::hash(payload,QCryptographicHash::Sha256).toHex()))))
        return fail(message,QStringLiteral("设备入口只接受原设备包来源；用户请求不得借伪Tt/TR进入事务。"));
    if (payload.startsWith("Ur")) return reagents_.applyDeviceReply(payload, message);
    if (payload.startsWith("TR")) return receiveTaskErrors(payload,message,basis,receipt,committed);
    if (payload.startsWith("TB"))
    {
        QMap<QString,int> seconds;
        if (!parseRackTimes(payload,&seconds,message)) return false;
        samples_->updateRackTimes(seconds);
        return true;
    }
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
                if (taskErrors_.contains(task.taskId))
                {
                    // 错误后的迟到状态不能恢复任务；出现已加样证据须交人工核对余量。
                    if (progress.state==QStringLiteral("8") && !addedReagentTasks_.contains(task.taskId))
                        return fail(message,QStringLiteral("错误任务又返回已加试剂状态，保留错误与原退量记录，须核对设备实际液量。"));
                    return true;
                }
                if (finishedTasks_.contains(task.taskId)) return true;
                // 原3字段通知没有阶段，不据此猜测已加试剂或改变任务状态。
                if (progress.state.isEmpty()) return true;
                bool stateOk=false;
                const int state=progress.state.toInt(&stateOk);
                if (!stateOk || progress.state!=QString::number(state) || state<0 || state>9 || progress.type!=task.type)
                    return fail(message,QStringLiteral("设备加样阶段或任务类型未确认，未修改状态与退量标志。"));
                if (state==8) addedReagentTasks_.insert(task.taskId);
                if (taskProgress_.contains(task.taskId) && state<taskProgress_.value(task.taskId)) return true;
                if (task.type == QStringLiteral("CAL") && state > 0 && state < 9)
                {
                    static const QStringList states{QString(),QStringLiteral("加样中"),QStringLiteral("样本育温"),QStringLiteral("加R0中"),QStringLiteral("R0育温"),QStringLiteral("加R1中"),QStringLiteral("R1育温"),QStringLiteral("加终试剂"),QStringLiteral("测试中")};
                    Database database;
                    if (!database.open(root_, QStringLiteral("result/results.db"), true, message) ||
                        !OriginalControlTasks::attachCalibration(database, root_, message) || !database.begin(message) ||
                        !OriginalControlTasks::calibrationState(database, task, states[state], message) || !database.commit(message)) return false;
                }
                taskProgress_[task.taskId]=state;
                const auto next=state==0 ? SampleTaskState::Idle : state==9 ? SampleTaskState::Finished : SampleTaskState::Testing;
                return samples_->updateDeviceTask(task,next,message);
            }
        return fail(message, QStringLiteral("设备带回了本次会话未下发的进度任务号，未推测对应样本。"));
    }
    if (!payload.startsWith("Tr")) return fail(message, QStringLiteral("不是已接入的原设备数据消息。"));
    ReceivedResult result;
    if (!parseResult(payload, &result, message)) return false;
    const Task *known = nullptr;
    for (const auto &task : issuedTasks_) if (task.taskId == result.taskId) { known = &task; break; }
    if (!known) return fail(message, QStringLiteral("结果任务号不在本次已下发清单，未生成新样本或猜测归属。"));
    if (taskErrors_.contains(known->taskId))
        return fail(message,QStringLiteral("该任务已由TR处理错误或复位，迟到Tr不能覆盖错误或再次使用已退量任务。"));
    bool holeOk = false;
    const int hole = result.fields.value(QStringLiteral("sample_hole")).toInt(&holeOk);
    if (result.fields.value(QStringLiteral("rack_id")) != known->rack || !holeOk || hole != known->hole ||
        result.fields.value(QStringLiteral("project_name")) != known->project.name ||
        result.fields.value(QStringLiteral("sample_id")) != known->sample ||
        result.fields.value(QStringLiteral("taskType")) != known->type ||
        (known->type != QStringLiteral("SAM") &&
         (result.fields.value(QStringLiteral("sample_rate")) != known->rate || result.fields.value(QStringLiteral("batch_group")) != known->batchGroup)))
        return fail(message, QStringLiteral("结果的样本架、孔位、条码、类型或项目与原下发任务不符，未写到其他样本。"));
    const QString digest = QString::fromLatin1(QCryptographicHash::hash(payload,QCryptographicHash::Sha256).toHex());
    const auto stored = storedResultPayloadHashes_.constFind(known->taskId);
    if (stored != storedResultPayloadHashes_.cend())
    {
        if (stored.value() != digest)
            return fail(message, QStringLiteral("该任务已完整保存另一份原始结果，重复返回内容不同，未覆盖或改动新预设。"));
        if (message) *message = QStringLiteral("该任务相同原始结果已完整保存，重复返回未改动当前样本预设。");
        return true;
    }
    if (!OriginalBusiness::workingRoot(root_, message)) return false;
    // 原saveResultInformation把QC最终曲线写入月份目录；CAL用calibration。
    // SAM现有日目录兼容问题另列，不在本窗口更改其已经接通的路径。
    const QString relative = QStringLiteral("result/") + (known->type == QStringLiteral("CAL") ? QStringLiteral("calibration") :
        QDate::currentDate().toString(known->type == QStringLiteral("QC") ? QStringLiteral("yyyy-MM") : QStringLiteral("yyyy-MM-dd")));
    const QString folder = QDir(root_).filePath(relative);
    for (const auto &path : {QDir(root_).filePath(QStringLiteral("result")), folder})
        if (QFileInfo(path).isSymLink() || (QFileInfo::exists(path) && !QFileInfo(path).isDir()))
            return fail(message, QStringLiteral("结果目录不是普通文件夹，或经过了符号链接。"));
    if (!QDir(root_).mkpath(relative)) return fail(message, QStringLiteral("无法创建原格式结果日期目录，原始结果未保存。"));
    const QString target = QDir(folder).filePath(QString::number(known->taskId));
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
            if (known->type != QStringLiteral("CAL")) settings.setValue(QStringLiteral("base/row_id"),known->reportRow);
            if (known->type == QStringLiteral("QC"))
            {
                settings.setValue(QStringLiteral("quality/controlName"), known->quality.controlName);
                settings.setValue(QStringLiteral("quality/batchGroup"), known->quality.batchName);
                settings.setValue(QStringLiteral("rk_quality/dataset"), known->quality.datasetId);
                settings.setValue(QStringLiteral("rk_quality/control_id"), known->quality.controlId);
                settings.setValue(QStringLiteral("rk_quality/batch_id"), known->quality.batchId);
                settings.setValue(QStringLiteral("rk_quality/association_id"), known->qualityAssociationId);
            }
            if (known->type == QStringLiteral("CAL"))
            {
                settings.setValue(QStringLiteral("calibration/row_id"), known->calibrationPoint - 1);
                settings.setValue(QStringLiteral("calibration/row_id_max"), known->calibration.autoDilution ? known->calibration.pointCount * known->calibration.repeatCount : known->calibration.repeatCount);
                settings.setValue(QStringLiteral("rk_calibration/calibration_id"), known->calibrationId);
                settings.setValue(QStringLiteral("rk_calibration/point_index"), known->calibrationPoint);
                settings.setValue(QStringLiteral("rk_calibration/repeat_index"), known->repeatIndex);
                settings.setValue(QStringLiteral("rk_calibration/calibrator_batch"), known->calibration.calibratorBatch);
                settings.setValue(QStringLiteral("rk_calibration/reagent_batch_group"), known->calibration.reagentBatchGroup);
                settings.setValue(QStringLiteral("rk_calibration/concentration"), known->calibrationConcentration);
            }
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
    if (!database.open(root_,QStringLiteral("result/results.db"),true,message)) return false;
    if (known->type == QStringLiteral("CAL") && !OriginalControlTasks::attachCalibration(database, root_, message)) return false;
    if (!database.begin(message)) return false;
    if (known->type == QStringLiteral("CAL"))
    {
        if (!OriginalControlTasks::calibrationState(database, *known, QStringLiteral("待计算"), message)) return false;
    }
    else if (!database.execute(QStringLiteral("UPDATE report_data SET state=? WHERE row_id=? AND sample_id=? AND type=? AND EXISTS(SELECT 1 FROM report_results WHERE task_id=? AND fid=report_data.row_id AND project_name=?)"),
        {QStringLiteral("已收到曲线，待计算"),known->reportRow,known->sample,known->type == QStringLiteral("QC") ? 1 : 0,known->taskId,known->project.name},message,1)) return false;
    if (!database.commit(message)) return false;
    if (!samples_->updateDeviceTask(*known,SampleTaskState::Finished,message)) return false;
    storedResultPayloadHashes_.insert(known->taskId,digest);
    finishedTasks_.insert(known->taskId);
    reservedReagentRows_.remove(known->taskId);
    releaseFinishedRack(known->rack);
    return true;
}

bool OriginalDetectionSession::reagentConfiguration(QByteArray *payload, QString *message)
{
    QLockFile projectLock(QDir(root_).filePath(QStringLiteral(".sn100-original-project-write.lock")));
    QLockFile reagentLock(QDir(root_).filePath(QStringLiteral(".sn100-original-reagent-write.lock")));
    if (!projectLock.tryLock(0) || !reagentLock.tryLock(0))
        return fail(message,QStringLiteral("原项目或试剂正在保存，未下发旧盘面资料。"));
    OriginalReagentState state;
    if (!reagents_.load(&state,message)) return false;
    if (pageMetadata_)
    {
        OriginalReagentPageMetadata::View view;
        if (!pageMetadata_->capture(state.root,state.holes,&view,message)) return false;
        for (auto &row : state.holes)
            row[QStringLiteral("reagent_batch")] = view.batches.value(row.value(QStringLiteral("id")).toLongLong());
        if (!pageMetadata_->current(view,message)) return false;
    }
    return reagents_.reagentUpdatePayload(state,payload,message);
}

void OriginalDetectionSession::markConnectionUncertain()
{
    cancelUnsent();
    for (const auto &task : issuedTasks_)
        if (!finishedTasks_.contains(task.taskId)) uncertainTasks_.insert(task.taskId);
}

void OriginalDetectionSession::releaseFinishedRack(const QString &name)
{
    for (const auto &task : issuedTasks_)
        if (task.rack==name && !finishedTasks_.contains(task.taskId)) return;
    for (const auto &rack : samples_->snapshot().racks)
        if (rack.name==name) samples_->setRackInFlight(rack.id,false);
}

bool OriginalDetectionSession::rackConfiguration(QByteArray *payload, QString *message)
{
    SampleSnapshot snapshot;
    if (!payload || !samples_->load(&snapshot,message)) return false;
    *payload=QByteArrayLiteral("TF");
    for (const auto &rack : snapshot.racks)
        if (rack.emergency && rack.name==QStringLiteral("Emergency"))
            for (const auto &hole : rack.holes)
                if (sampleHoleState(sampleHoleTaskStates(hole))==SampleHoleState::Ready)
                { *payload=QByteArrayLiteral("TF[Emergency]"); return true; }
    return true;
}

bool OriginalDetectionSession::receiveTaskErrors(const QByteArray &payload, QString *message,
    const OriginalTaskReagents::RuntimeBasis &basis, OriginalTaskReagents::Commit *result,
    OriginalTaskReagents::CommitHandler committed)
{
    QLockFile projectLock(QDir(root_).filePath(QStringLiteral(".sn100-original-project-write.lock")));
    QLockFile reagentLock(QDir(root_).filePath(QStringLiteral(".sn100-original-reagent-write.lock")));
    projectLock.setStaleLockTime(0); reagentLock.setStaleLockTime(0);
    OriginalTaskReagents::ReceiptWriter receipt(OriginalTaskReagents::Action::UnusedReservationReturn,root_,payload,basis,result,committed);
    ReceivedTaskError error;
    if (!parseTaskError(payload,&error,message)) return false;
    QList<Task> pending;
    // 先核整包；一个未知任务不能造成其他任务先退量再报错。
    const auto snapshot=samples_->snapshot();
    for (const auto &entry : error.tasks)
    {
        const Task *known=nullptr;
        for (const auto &task : issuedTasks_) if (task.taskId==entry.taskId) { known=&task; break; }
        if (!known || known->rack!=entry.rack || known->hole!=entry.hole)
            return fail(message,QStringLiteral("TR架、孔或任务号与本进程已下发任务不符，整包未改动。"));
        if (taskErrors_.contains(entry.taskId))
        {
            if (taskErrors_.value(entry.taskId)!=error.reason)
                return fail(message,QStringLiteral("同一任务返回了不同错误原因，保留原处理结果，须核对设备；未再次退量。"));
            receipt.data().alreadyHandledTaskIds.append(entry.taskId);
            continue;
        }
        if (finishedTasks_.contains(entry.taskId) || taskProgress_.value(entry.taskId,-1)==9)
            return fail(message,QStringLiteral("已收到曲线或结束阶段的任务又返回TR，未覆盖结果或退量。"));
        if (uncertainTasks_.contains(entry.taskId) && !addedReagentTasks_.contains(entry.taskId))
        {
            receipt.data().refusedUncertainTaskIds.append(entry.taskId);
            return fail(message,QStringLiteral("任务经历连接失效，是否已经加试剂不明确，未自动退量或重发。"));
        }
        bool found=false;
        for (const auto &rack : snapshot.racks)
            if (rack.name==known->rack)
                for (const auto &hole : rack.holes)
                    if (hole.number==known->hole)
                        for (const auto &preset : hole.tasks)
                            if (preset.projectId==known->project.id && preset.calibrationPoint==known->calibrationPoint &&
                                hole.sampleType==(known->type==QStringLiteral("CAL")?1:known->type==QStringLiteral("QC")?2:0) &&
                                known->repeatIndex>=0 && known->repeatIndex<sampleTaskStates(preset).size()) found=true;
        if (!found) return fail(message,QStringLiteral("TR对应预设已经变化，未将旧任务写入新预设。"));
        pending.append(*known);
    }
    if (pending.isEmpty())
    { if (message) *message=QStringLiteral("重复TR已核对，未重复写结果或退还试剂。"); return true; }
    if (!projectLock.tryLock(0) || !reagentLock.tryLock(0))
        return fail(message,QStringLiteral("原项目或试剂正在保存，TR尚未提交退量，未自动重发或补偿，请核对。"));
    if (!OriginalProjectCopy::noPending(root_,message)) return false;
    OriginalReagentState observed;
    if (!reagents_.load(&observed,message)) return false;
    QMap<qint64,QString> finalUsedText;
    if (!refundTexts(observed,pending,addedReagentTasks_,reservedReagentRows_,&finalUsedText,message)) return false;
    for (const auto &task : pending)
    {
        auto origin=OriginalTaskReagents::taskOrigin(task);
        origin.skippedAdded=addedReagentTasks_.contains(task.taskId);
        if (origin.skippedAdded) receipt.data().skippedAddedTaskIds.append(task.taskId);
        else origin.returnedByHole=task.reservedReagentVolumes;
        receipt.data().tasks.append(origin);
    }
    {
        Database database;
        if (!database.open(root_,QStringLiteral("result/results.db"),true,message)) return false;
        const QString uri=QUrl::fromLocalFile(QDir(root_).filePath(QStringLiteral("db/userSet/reagent_Setting.db"))).toString(QUrl::FullyEncoded)+QStringLiteral("?mode=rw");
        if (!database.execute(QStringLiteral("ATTACH DATABASE ? AS original_reagents"),{uri},message)) return false;
        bool hasCalibration = false;
        for (const auto &task : pending) if (task.type == QStringLiteral("CAL")) hasCalibration = true;
        if (hasCalibration && !OriginalControlTasks::attachCalibration(database, root_, message)) return false;
        for (const auto &schema : {QStringLiteral("main"),QStringLiteral("original_reagents")})
        {
            QSqlQuery query(database.db);
            if (!query.exec(QStringLiteral("PRAGMA %1.journal_mode").arg(schema)) || !query.next())
                return fail(message,QStringLiteral("无法核对错误恢复事务模式。"));
            const QString mode=query.value(0).toString().toLower();
            if (mode!=QStringLiteral("delete") && mode!=QStringLiteral("truncate") && mode!=QStringLiteral("persist"))
                return fail(message,QStringLiteral("当前库模式尚不能保证报告与退量一起提交，未改变模式或部分恢复。"));
        }
        if (!database.begin(message)) return false;
        receipt.transactionStarted();
        if (!versionMatches(observed,message)) return false;
        receipt.captureBefore(database.db,observed);
        const QStringList reasons{QStringLiteral(""),QStringLiteral("错误"),QStringLiteral("缺样本"),QStringLiteral("缺样本清洗液"),
            QStringLiteral("缺稀释液"),QStringLiteral("缺试剂"),QStringLiteral("缺试剂清洗液"),QStringLiteral("缺耗材")};
        const QString reason=reasons[error.reason];
        for (const auto &task : pending)
        {
            // 原TR不创建校准的普通报告行；原点表也没有TR专门写入分支。
            // 只核该点真实占号关联，保留原点资料；孔位错误与退量仍按同一链处理。
            if (task.type == QStringLiteral("CAL"))
            { if (!OriginalControlTasks::calibrationState(database, task, QString(), message)) return false; continue; }
            if (!database.execute(QStringLiteral("UPDATE report_results SET original_value=?,calibration_value=?,rate_value=?,inr_value=?,mda_value=?,formula_value=? WHERE task_id=? AND fid=? AND project_name=?"),
                {reason,reason,reason,reason,reason,reason,task.taskId,task.reportRow,task.project.name},message,1) ||
                !database.execute(QStringLiteral("UPDATE report_data SET state=? WHERE row_id=? AND sample_id=? AND type=?"),
                {QStringLiteral("测试失败"),task.reportRow,task.sample,task.type == QStringLiteral("QC") ? 1 : 0},message,1)) return false;
        }
        for (const auto &row : observed.holes)
        {
            const qint64 id=row.value(QStringLiteral("id")).toLongLong();
            if (!finalUsedText.contains(id)) continue;
            const QString usedText=finalUsedText.value(id);
            if (!database.execute(QStringLiteral("UPDATE original_reagents.\"%1\" SET used_ul=? WHERE id=? AND used_ul=?").arg(observed.settingTable),
                {usedText,id,row.value(QStringLiteral("used_ul"))},message,1)) return false;
            receipt.matchedUsedWrite(row,usedText);
        }
        receipt.captureTransactionAfter(database.db); // 真实亲和转换/typeof先在原事务内取证。
        if (!database.commit(message)) { receipt.commitFailed(); return false; }
        receipt.commitSeen();
    } // writer已关闭，after取证和回调仍在两锁内。
    // 整包已COMMIT的每个task先保留终态；后续一个内存更新失败不能留下其他任务再次退量资格。
    for (const auto &task : pending)
    {
        taskErrors_.insert(task.taskId,error.reason);
        finishedTasks_.insert(task.taskId);
        reservedReagentRows_.remove(task.taskId);
    }
    receipt.finish();
    for (const auto &task : pending)
    {
        if (!samples_->updateDeviceTask(task,error.reason==0 ? SampleTaskState::Reset : SampleTaskState::Error,message)) return false;
        releaseFinishedRack(task.rack);
    }
    if (message) *message=QStringLiteral("TR已按原原因更新报告和任务；仅对尚未标记TD8的任务按原配置量退账，未重发任务。");
    return true;
}
