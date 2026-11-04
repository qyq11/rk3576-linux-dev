#include "original_sample_repository.h"
#include "original_calibration_preset.h"
#include "original_files/original_business_database.h"
#include "original_files/original_ini.h"
#include <QDir>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QLockFile>
#include <QElapsedTimer>
#include <limits>

namespace
{
bool fail(QString *message, const QString &text)
{
    if (message) *message = text;
    return false;
}
bool plain(const QString &value, int limit, bool empty = false)
{
    if ((!empty && value.isEmpty()) || value != value.trimmed() || value.size() > limit) return false;
    for (const auto character : value)
        if (character.category() == QChar::Other_Control || character == '[' || character == ']') return false;
    return true;
}
const ProjectRecord *findProject(const ProjectRecords &records, qint64 id)
{
    for (const auto &project : records) if (project.id == id) return &project;
    return nullptr;
}
}

bool OriginalSampleRepository::refreshProjects(QString *message)
{
    QStringList reagents, cleaners, diluents;
    return projectRepository_.load(&projects_, &units_, &reagents, &cleaners, &diluents, message);
}

bool OriginalSampleRepository::initialize(const QString &root, QString *message, bool newHoleContainerIsTube,
    OriginalIni::InitializationSourcePtr configurationSource)
{
    initialized_ = false;
    configurationSource_.reset();
    if (!OriginalBusiness::workingRoot(root, message)) return false;
    if (configurationSource)
    {
        bool configuredTube=false;
        if (!configurationSource->matchesRoot(root,message) ||
            !configurationSource->defaultContainerIsTube(&configuredTube,message)) return false;
        if (configuredTube!=newHoleContainerIsTube)
            return fail(message,QStringLiteral("新孔容器默认值与本次初始化配置不一致，未给旧参数采用新来源。"));
    }
    if (!projectRepository_.initialize(root, {}, message)) return false;
    OriginalIni::Settings theme;
    if (configurationSource) theme=configurationSource->theme();
    else if (!OriginalIni::read(QDir(root).filePath(QStringLiteral("theme_config.ini")), &theme, message)) return false;
    bool normalOk = false, emergencyOk = false;
    ordinaryHoles_ = theme.values.value(QStringLiteral("theme/SampleHoleCount"), 8).toInt(&normalOk);
    emergencyHoles_ = theme.values.value(QStringLiteral("theme/EmergencyHoleCount"), 3).toInt(&emergencyOk);
    if (!normalOk || !emergencyOk || ordinaryHoles_ < 1 || ordinaryHoles_ > 128 || emergencyHoles_ < 1 || emergencyHoles_ > 128)
        return fail(message, QStringLiteral("原配置中的普通/急诊孔数非法，未套用旧页面固定孔数。"));
    root_ = root;
    snapshot_ = SampleSnapshot();
    SampleRack emergency;
    emergency.id = 1;
    emergency.name = QStringLiteral("Emergency");
    emergency.emergency = true;
    for (int i = 1; i <= emergencyHoles_; ++i)
    {
        SampleHole hole;
        hole.number = i;
        hole.container = newHoleContainerIsTube ? 0 : 1; // 仅本次真正创建的急诊孔。
        emergency.holes.append(hole);
    }
    snapshot_.racks.append(emergency);
    nextRackId_ = 2;
    inFlight_.clear();
    if (!load(&snapshot_, message)) return false;
    configurationSource_=std::move(configurationSource); // 只记录本仓库真正消费并成功初始化的那一份。
    initialized_ = true;
    return true;
}

bool OriginalSampleRepository::load(SampleSnapshot *snapshot, QString *message)
{
    if (!snapshot || !refreshProjects(message)) return false;
    const QString file = QDir(root_).filePath(QStringLiteral("db/userSet/sample_Combination.db"));
    const QString before = OriginalProjectRepository::databaseFingerprint(file, message);
    if (before.isEmpty()) return false;
    OriginalBusiness::Database database;
    OriginalBusiness::Rows rows;
    if (!database.open(root_, QStringLiteral("db/userSet/sample_Combination.db"), false, message) ||
        !database.rows(QStringLiteral("sample_combination"), {QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("project_contain")}, &rows, message)) return false;
    QList<SampleCombination> combinations;
    for (const auto &row : rows)
    {
        SampleCombination combination;
        combination.id = row.value(QStringLiteral("id")).toLongLong();
        combination.name = row.value(QStringLiteral("name")).toString();
        if (combination.name.contains(QStringLiteral("瓶瓶-"))) combination.mode = SampleCombinationMode::MultiReagentBottle;
        const QString joined = row.value(QStringLiteral("project_contain")).toString();
        if (!joined.isEmpty())
            for (const auto &name : joined.split(';', Qt::KeepEmptyParts))
            {
                qint64 id = 0;
                for (const auto &project : projects_) if (project.name == name) { id = project.id; break; }
                if (!id) return fail(message, QStringLiteral("原组合引用了不存在的项目：") + name + QStringLiteral("；未丢弃该成员。"));
                combination.orderedProjectIds.append(id);
            }
        combinations.append(combination);
    }
    if (before != OriginalProjectRepository::databaseFingerprint(file, message))
        return fail(message, QStringLiteral("读取期间原样本组合改变，请重新读取。"));
    combinationVersion_ = before;
    snapshot_.combinations = combinations;
    *snapshot = snapshot_;
    return true;
}

bool OriginalSampleRepository::editable(qint64 id, QString *message) const
{
    if (inFlight_.contains(id)) return fail(message, QStringLiteral("该样本架有已提交或执行中的任务，请先完成或核对仪器状态。"));
    for (const auto &rack : snapshot_.racks)
        if (rack.id == id)
            for (const auto &hole : rack.holes)
                if (sampleHoleState(sampleHoleTaskStates(hole)) == SampleHoleState::Testing)
                    return fail(message, QStringLiteral("样本架正在测试，不能修改或删除。"));
    return true;
}

bool OriginalSampleRepository::validateRack(const SampleRack &rack, QString *message) const
{
    static const QRegularExpression normalName(QStringLiteral("^S(0[1-9]|[1-8][0-9]|9[0-8])$"));
    if ((rack.emergency && rack.name != QStringLiteral("Emergency")) || (!rack.emergency && !normalName.match(rack.name).hasMatch()) ||
        rack.holes.size() != (rack.emergency ? emergencyHoles_ : ordinaryHoles_))
        return fail(message, QStringLiteral("样本架号或孔位数与原配置不符。"));
    QSet<int> holes;
    for (const auto &hole : rack.holes)
    {
        if (holes.contains(hole.number) || hole.number < 1 || hole.number > rack.holes.size() ||
            (hole.container != 0 && hole.container != 1) || !plain(hole.sampleId, 128, hole.tasks.isEmpty()))
            return fail(message, QStringLiteral("样本孔位重复、容器类型或样本编号不合法。"));
        holes.insert(hole.number);
        if (!validateControlHole(hole, rack.holes.size(), message)) return false;
        QSet<QString> seen;
        for (const auto &task : hole.tasks)
        {
            const auto *project = findProject(projects_, task.projectId);
            const QString identity = QString::number(task.projectId) + ':' + QString::number(hole.sampleType == 1 ? task.calibrationPoint : 0);
            if (!project || !project->enabled || seen.contains(identity) || task.repeats < 1 || task.repeats > 99 ||
                (!task.states.isEmpty() && task.states.size() != task.repeats))
                return fail(message, QStringLiteral("样本引用项目已失效、项目重复或重复次数不合法。"));
            seen.insert(identity);
            for (const auto state : sampleTaskStates(task))
                if (state != SampleTaskState::Idle && state != SampleTaskState::Reset && state != SampleTaskState::Testing &&
                    state != SampleTaskState::Finished && state != SampleTaskState::Error)
                    return fail(message, QStringLiteral("样本任务状态未识别，未保存草稿。"));
            bool rate = false;
            for (const auto &option : project->sampleRates)
                if (option.sampleRate == task.rate && (hole.sampleType == 1 || option.sampleOption)) rate = true;
            if (!rate) return fail(message, QStringLiteral("样本所选稀释方案已不存在或未启用。"));
        }
    }
    QMap<qint64, QString> projectGroups;
    for (const auto &hole : rack.holes)
    {
        if (hole.sampleType != 1) continue;
        const auto &preset = hole.calibration;
        if (projectGroups.contains(preset.projectId) && projectGroups.value(preset.projectId) != preset.groupToken)
            return fail(message, QStringLiteral("同架同项目存在不同校准草稿，未合并为同一原校准编号。"));
        projectGroups[preset.projectId] = preset.groupToken;
        for (int point = 0; point < preset.pointCount; ++point)
        {
            const SampleHole *targetHole = nullptr;
            for (const auto &candidate : rack.holes) if (candidate.number == preset.holes[point]) targetHole = &candidate;
            if (!targetHole) return fail(message, QStringLiteral("校准点对应孔位不存在，整组未保存。"));
            const auto &target = *targetHole;
            if (target.sampleType != 1 || target.calibration.groupToken != preset.groupToken ||
                target.calibration.calibratorBatch != preset.calibratorBatch ||
                target.calibration.reagentBatchGroup != preset.reagentBatchGroup ||
                target.calibration.rates != preset.rates || target.calibration.concentrations != preset.concentrations ||
                target.calibration.holes != preset.holes)
                return fail(message, QStringLiteral("校准点所属草稿不完整或批号不一致，整组未保存。"));
            int matches = 0;
            for (const auto &task : target.tasks) if (task.calibrationPoint == point + 1) ++matches;
            if (matches != 1) return fail(message, QStringLiteral("校准点缺失或重复，整组未保存。"));
        }
    }
    return true;
}

bool OriginalSampleRepository::qualityChoices(SampleQcPreset::Snapshot *choices, QString *message) const
{
    if (!choices) return fail(message, QStringLiteral("没有质控选择输出位置。"));
    const QString file = QDir(root_).filePath(QStringLiteral("result/quality.db"));
    const QString before = OriginalProjectRepository::databaseFingerprint(file, message);
    if (before.isEmpty()) return false;
    OriginalBusiness::Database database;
    OriginalBusiness::Rows controls, batches, associations;
    if (!database.open(root_, QStringLiteral("result/quality.db"), false, message) ||
        !database.rows(QStringLiteral("quality_control_list"), {"id","item_name"}, &controls, message) ||
        !database.rows(QStringLiteral("quality_control_batch"), {"id","fid","batch_name"}, &batches, message) ||
        !database.rows(QStringLiteral("quality_item_proj"), {"id","item_id","proj_name"}, &associations, message)) return false;
    SampleQcPreset::Snapshot result;
    result.originalRoot = root_;
    result.datasetId = QStringLiteral("original-quality:") + root_;
    for (const auto &project : projects_) result.projects.append({project.id, project.name, project.enabled});
    for (const auto &row : controls)
    {
        SampleQcPreset::Control control;
        control.id = row.value("id").toLongLong(); control.name = row.value("item_name").toString();
        for (const auto &batch : batches) if (batch.value("fid").toLongLong() == control.id)
            control.batches.append({batch.value("id").toLongLong(), control.id, batch.value("batch_name").toString()});
        for (const auto &association : associations) if (association.value("item_id").toLongLong() == control.id)
        {
            SampleQcPreset::Association ref;
            ref.id = association.value("id").toLongLong(); ref.controlId = control.id;
            ref.projectName = association.value("proj_name").toString();
            for (const auto &project : projects_) if (project.name == ref.projectName)
            {
                if (ref.projectId) return fail(message, QStringLiteral("原质控项目名称对应多个项目编号，未猜测选择。"));
                ref.projectId = project.id;
            }
            control.projects.append(ref); // 失效关联也保留，提交时明确报错，不能悄悄丢掉。
        }
        result.controls.append(control);
    }
    if (before != OriginalProjectRepository::databaseFingerprint(file, message))
        return fail(message, QStringLiteral("读取原质控选择时资料改变，请重读，原草稿保留。"));
    result.ready = true;
    *choices = result;
    return true;
}

bool OriginalSampleRepository::validateControlHole(const SampleHole &hole, int holeCount, QString *message) const
{
    if (hole.sampleType == 0)
    {
        if (!hole.quality.selected.datasetId.isEmpty() || !hole.calibration.groupToken.isEmpty())
            return fail(message, QStringLiteral("普通样本仍带校准或质控身份，请先删除任务后重新设置。"));
        return true;
    }
    if (hole.sampleType == 2)
    {
        SampleQcPreset::Snapshot choices;
        if (!qualityChoices(&choices, message)) return false;
        const auto checked = SampleQcPreset::recheck(choices, hole.quality.selected);
        if (!checked.ok()) return fail(message, checked.message);
        if (hole.sampleId != checked.selection.sampleId || !hole.calibration.groupToken.isEmpty())
            return fail(message, QStringLiteral("质控样本号与当前原控制品、批次身份不符。"));
        if (hole.tasks.size() != checked.selection.projects.size())
            return fail(message, QStringLiteral("质控任务项目与原控制品关联不完整。"));
        QSet<qint64> allowed;
        for (const auto &ref : checked.selection.projects) allowed.insert(ref.projectId);
        for (const auto &task : hole.tasks)
            if (!allowed.remove(task.projectId) || task.calibrationPoint != 0 || !task.calibrationConcentration.isEmpty())
                return fail(message, QStringLiteral("质控任务混入了其他项目或校准点。"));
        return true;
    }
    if (hole.sampleType != 1) return fail(message, QStringLiteral("原样本类型不是普通、校准或质控，未下发。"));
    const auto &preset = hole.calibration;
    const auto *project = findProject(projects_, preset.projectId);
    if (!project) return fail(message, QStringLiteral("校准项目已不存在，原草稿保留。"));
    if (!OriginalCalibrationPreset::validate(*project, preset, holeCount, message)) return false;
    if (hole.sampleId != QStringLiteral("c-") + preset.calibratorBatch || !hole.quality.selected.datasetId.isEmpty())
        return fail(message, QStringLiteral("校准样本号、批号或类型身份不一致。"));
    for (const auto &task : hole.tasks)
    {
        const int point = task.calibrationPoint - 1;
        if (task.projectId != preset.projectId || point < 0 || point >= preset.pointCount ||
            preset.holes[point] != hole.number || task.rate != preset.rates[point] ||
            task.repeats != preset.repeatCount || task.calibrationConcentration != preset.concentrations[point] || task.prolonged)
            return fail(message, QStringLiteral("校准点、浓度、重复次数或孔位与原草稿不符。"));
    }
    if (project->originalBaseValues.value("is_related").toInt() > 0)
    {
        OriginalBusiness::Database database; OriginalBusiness::Rows groups;
        if (!database.open(root_, QStringLiteral("db/userSet/reagent_Information.db"), false, message) ||
            !database.rows("reagent_batch_group", {"id","project_name","batch_group"}, &groups, message)) return false;
        bool found = false;
        for (const auto &group : groups)
            if (group.value("project_name").toString() == project->name &&
                group.value("batch_group").toString() == preset.reagentBatchGroup && preset.reagentBatchGroup != QStringLiteral("PTFbg")) found = true;
        if (!found) return fail(message, QStringLiteral("原项目的校准试剂组已不存在，请重读后选择。"));
    }
    return true;
}

bool OriginalSampleRepository::saveCalibrationParameters(qint64 projectId, QString expectedVersion, QString lastValues, QString *message,
    OriginalProjectSaveNotice *result, const std::function<void(const OriginalProjectSaveNotice &)> &committedNotice,
    const QString &expectedFileName)
{
    OriginalProjectSaveNotice localResult;
    if (!result) result=&localResult;
    *result=OriginalProjectSaveNotice();
    const auto notifyCommitted=[&]()
    {
        // 仍持本文件的原写锁，先排通知再允许下一保存者提交；回调不执行SQL。
        if (result->commitSeen && committedNotice) committedNotice(*result);
    };
    if (!refreshProjects(message)) return false;
    const auto *project = findProject(projects_, projectId);
    if (!project || project->originalFingerprint != expectedVersion)
        return fail(message, QStringLiteral("打开校准设置后原项目已变化，未覆盖上次浓度参数。"));
    const ProjectRecord target=*project; // refreshProjects会替换容器，不能在刷新后再用旧指针。
    if (!expectedFileName.isEmpty() && target.originalFileName!=expectedFileName)
        return fail(message,QStringLiteral("容量预检后项目原文件名已改变，未改为保存同编号的另一文件；原草稿保留。"));
    if (result)
    {
        result->root=root_; result->originalFileName=target.originalFileName;
        result->projectId=target.id; result->projectName=target.name; result->beforeFingerprint=expectedVersion;
    }
    QLockFile lock(QDir(root_).filePath(QStringLiteral(".sn100-original-project-write.lock")));
    if (!lock.tryLock(0)) return fail(message, QStringLiteral("原项目正在由其他操作保存，请保留校准草稿后重试。"));
    if (!projectRepository_.writesAllowed(message)) return false;
    {
        OriginalBusiness::Database database;
        if (!database.open(root_, QStringLiteral("project/") + target.originalFileName, true, message) || !database.begin(message)) return false;
        if (OriginalProjectRepository::databaseFingerprint(QDir(root_).filePath(QStringLiteral("project/") + target.originalFileName), message) != expectedVersion)
            return fail(message, QStringLiteral("写入前原项目资料改变，未覆盖。"));
        // 原project_base是name/value逐键保存，项目编号已由文件和读取版本核对。
        if (!database.execute(QStringLiteral("UPDATE project_base SET value=? WHERE name='last_cal_para'"), {lastValues}, message, 1) || !database.commit(message)) return false;
        if (result) result->commitSeen=true;
    } // 写连接已关闭；仍持原锁，随后取得不会因本连接关闭而改变的文件/WAL指纹。
    if (!refreshProjects(message)) { notifyCommitted(); return false; }
    const ProjectRecord *committed=nullptr;
    for (const auto &value : projects_)
        if (value.originalFileName==target.originalFileName)
        {
            if (committed)
            {
                fail(message,QStringLiteral("已提交校准参数，但目标原文件身份重复，设备版本未确认。"));
                notifyCommitted(); return false;
            }
            committed=&value;
        }
    if (!committed || committed->id!=target.id || committed->name!=target.name || committed->originalFingerprint.isEmpty() ||
        committed->originalBaseValues.value(QStringLiteral("last_cal_para"))!=lastValues)
    {
        fail(message,QStringLiteral("已提交校准参数，但提交后目标身份或实际参数未能核实，设备版本未确认。"));
        notifyCommitted(); return false;
    }
    if (result) { result->verifiedSnapshot=true; result->committedFingerprint=committed->originalFingerprint; }
    notifyCommitted();
    return true;
}

bool OriginalSampleRepository::registerEmptyOrdinaryRack(int ordinaryHoles, bool containerIsTube, qint64 *rackId,
                                                        SampleSnapshot *snapshot, QString *message)
{
    if (!initialized_ || !rackId || !snapshot || ordinaryHoles_ < 1 || ordinaryHoles_ > 128 ||
        ordinaryHoles != ordinaryHoles_)
        return fail(message, QStringLiteral("原样本资料尚未完成初始化或普通孔数无效，未新增样本架。"));
    if (nextRackId_ < 2 || nextRackId_ == std::numeric_limits<qint64>::max())
        return fail(message, QStringLiteral("样本架编号已耗尽，未新增样本架。"));
    for (const auto &existing : snapshot_.racks)
        if (existing.id == nextRackId_)
            return fail(message, QStringLiteral("样本架编号重复，未覆盖已有样本架。"));

    SampleRack rack;
    for (int number = 1; number < 99; ++number)
    {
        const QString name = QStringLiteral("S%1").arg(number, 2, 10, QChar('0'));
        bool exists = false;
        for (const auto &existing : snapshot_.racks)
            if (existing.name == name) exists = true;
        if (!exists)
        {
            rack.name = name;
            break;
        }
    }
    if (rack.name.isEmpty())
        return fail(message, QStringLiteral("普通架已达98个本地预算。"));
    for (int number = 1; number <= ordinaryHoles_; ++number)
    {
        SampleHole hole;
        hole.number = number;
        hole.container = containerIsTube ? 0 : 1;
        rack.holes.append(hole);
    }
    // 这里只能由后端构造空架；校准、质控及真实任务保存仍走原saveRack校验。
    rack.id = nextRackId_++;
    snapshot_.racks.append(rack);
    *rackId = rack.id;
    *snapshot = snapshot_;
    return true;
}

bool OriginalSampleRepository::saveRack(SampleRack rack, qint64 *savedId, QString *message)
{
    if (!savedId || !refreshProjects(message) || !editable(rack.id, message) || !validateRack(rack, message)) return false;
    int index = -1;
    for (int i = 0; i < snapshot_.racks.size(); ++i)
    {
        const auto &existing = snapshot_.racks[i];
        if (existing.name == rack.name && existing.id != rack.id) return fail(message, QStringLiteral("样本架号已存在。"));
        if (existing.id == rack.id) index = i;
    }
    if (rack.id && index < 0) return fail(message, QStringLiteral("样本架已不存在，请重新读取。"));
    if (!rack.id)
    {
        if (rack.emergency || nextRackId_ == std::numeric_limits<qint64>::max()) return fail(message, QStringLiteral("急诊架不能重复创建或架编号已耗尽。"));
        rack.id = nextRackId_++;
        snapshot_.racks.append(rack);
    }
    else snapshot_.racks[index] = rack;
    *savedId = rack.id;
    return true;
}

bool OriginalSampleRepository::removeRack(qint64 id, QString *message)
{
    if (!editable(id, message)) return false;
    for (int i = 0; i < snapshot_.racks.size(); ++i)
        if (snapshot_.racks[i].id == id)
        {
            if (snapshot_.racks[i].emergency) return fail(message, QStringLiteral("急诊架不能删除。"));
            snapshot_.racks.removeAt(i);
            return true;
        }
    return fail(message, QStringLiteral("样本架已不存在。"));
}
bool OriginalSampleRepository::clearOrdinary(QString *message)
{
    for (const auto &rack : snapshot_.racks) if (!rack.emergency && !editable(rack.id, message)) return false;
    for (int i = snapshot_.racks.size() - 1; i >= 0; --i)
        if (!snapshot_.racks[i].emergency) snapshot_.racks.removeAt(i);
    return true;
}
void OriginalSampleRepository::setRackInFlight(qint64 id, bool inFlight)
{
    if (inFlight) inFlight_.insert(id); else inFlight_.remove(id);
}

bool OriginalSampleRepository::updateDeviceTask(const OriginalTasks::Task &task, SampleTaskState state, QString *message)
{
    for (auto &rack : snapshot_.racks)
        if (rack.name == task.rack)
            for (auto &hole : rack.holes)
                if (hole.number == task.hole)
                    for (auto &preset : hole.tasks)
                        if (preset.projectId == task.project.id && preset.calibrationPoint == task.calibrationPoint &&
                            hole.sampleType == (task.type == QStringLiteral("CAL") ? 1 : task.type == QStringLiteral("QC") ? 2 : 0))
                        {
                            auto states = sampleTaskStates(preset);
                            if (task.repeatIndex < 0 || task.repeatIndex >= states.size())
                                return fail(message, QStringLiteral("设备任务对应重复次数已改变，未更新其他任务状态。"));
                            states[task.repeatIndex] = state;
                            preset.states = states;
                            const auto aggregate=sampleHoleState(sampleHoleTaskStates(hole));
                            hole.revise = aggregate==SampleHoleState::Finished || aggregate==SampleHoleState::Error;
                            return true;
                        }
    return fail(message, QStringLiteral("设备任务对应的样本架/孔位/项目已不存在，原结果保留待核。"));
}

void OriginalSampleRepository::updateRackTimes(const QMap<QString, int> &seconds)
{
    QElapsedTimer clock; clock.start();
    for (auto &rack : snapshot_.racks)
        if (seconds.contains(rack.name))
        { rack.remainingSeconds=seconds.value(rack.name); rack.remainingAtMs=clock.msecsSinceReference(); }
}

bool OriginalSampleRepository::saveCombination(SampleCombination combination, qint64 *savedId, QString *message)
{
    if (!savedId || !plain(combination.name, 64) || combination.orderedProjectIds.size() > 1024 || !refreshProjects(message))
        return fail(message, QStringLiteral("组合名称、成员数或当前原项目数据不合法。"));
    QStringList names;
    for (qint64 id : combination.orderedProjectIds)
    {
        const auto *project = findProject(projects_, id);
        if (!project || !project->enabled) return fail(message, QStringLiteral("组合成员项目已不存在或未启用。"));
        if (project->name.contains(';')) return fail(message, QStringLiteral("项目名称包含原组合分隔符，不能无损保存为组合。"));
        names.append(project->name); // 原顺序和重复成员保持。
    }
    QString name = combination.name;
    if (combination.mode == SampleCombinationMode::MultiReagentBottle && !name.contains(QStringLiteral("瓶瓶-"))) name.prepend(QStringLiteral("瓶瓶-"));
    if (combination.mode == SampleCombinationMode::Ordinary && name.contains(QStringLiteral("瓶瓶-")))
        return fail(message, QStringLiteral("名称含原多瓶质控标记；普通组合请先修改名称。"));
    const QString path = QDir(root_).filePath(QStringLiteral("db/userSet/sample_Combination.db"));
    if (combinationVersion_ != OriginalProjectRepository::databaseFingerprint(path, message))
        return fail(message, QStringLiteral("原样本组合已由其他操作修改，请重新读取后再保存。"));
    OriginalBusiness::Database database;
    if (!database.open(root_, QStringLiteral("db/userSet/sample_Combination.db"), true, message) || !database.begin(message)) return false;
    if (combinationVersion_ != OriginalProjectRepository::databaseFingerprint(path, message))
        return fail(message, QStringLiteral("原样本组合在保存前已变化，请重新读取。"));
    const bool updated = combination.id
        ? database.execute(QStringLiteral("UPDATE sample_combination SET name=?,project_contain=? WHERE id=?"), {name, names.join(';'), combination.id}, message, 1)
        : database.execute(QStringLiteral("INSERT INTO sample_combination(name,project_contain) VALUES(?,?)"), {name, names.join(';')}, message, 1);
    if (!updated) return false;
    qint64 id = combination.id;
    if (!id)
    {
        QSqlQuery query(database.db);
        if (!query.exec(QStringLiteral("SELECT last_insert_rowid()")) || !query.next()) return fail(message, query.lastError().text());
        id = query.value(0).toLongLong();
    }
    if (!database.commit(message)) return false;
    combinationVersion_ = OriginalProjectRepository::databaseFingerprint(path, message);
    *savedId = id;
    return !combinationVersion_.isEmpty();
}

bool OriginalSampleRepository::removeCombination(qint64 id, QString *message)
{
    const QString path = QDir(root_).filePath(QStringLiteral("db/userSet/sample_Combination.db"));
    if (combinationVersion_ != OriginalProjectRepository::databaseFingerprint(path, message))
        return fail(message, QStringLiteral("原样本组合已由其他操作修改，请重新读取后再删除。"));
    OriginalBusiness::Database database;
    if (!database.open(root_, QStringLiteral("db/userSet/sample_Combination.db"), true, message) || !database.begin(message)) return false;
    if (combinationVersion_ != OriginalProjectRepository::databaseFingerprint(path, message))
        return fail(message, QStringLiteral("原样本组合在删除前已变化，请重新读取。"));
    if (!database.execute(QStringLiteral("DELETE FROM sample_combination WHERE id=?"), {id}, message, 1) || !database.commit(message)) return false;
    combinationVersion_ = OriginalProjectRepository::databaseFingerprint(path, message);
    return !combinationVersion_.isEmpty();
}
