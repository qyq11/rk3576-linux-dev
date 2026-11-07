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
#include <QCryptographicHash>
#include <QDataStream>
#include <QSqlRecord>
#include <QUuid>
#include <algorithm>
#include <QThread>
#include <QFileInfo>

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

QString digest(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

void writePatient(QDataStream &out, const SamplePatientItem &item)
{
    out << item.id << item.rowId << item.sampleNumber << item.reportType << item.statue
        << item.uploaded << item.isAutoId << item.sampleHole << item.barcode << item.dateTime << item.visible
        << item.sex << item.age << item.name << item.department << item.patientType << item.applicationSerial
        << item.bedNumber << item.jinayan << item.checker << item.auditor << item.remark << item.state;
}

QString holeTaskDigest(const SampleHole &hole)
{
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly); out.setVersion(QDataStream::Qt_5_12);
    out << QStringLiteral("SN100-LIS-hole-input-v2") << hole.number << hole.sampleId << hole.sampleBarcode
        << hole.isAutoId << hole.container << hole.needleType << hole.revise << hole.sampleType;
    writePatient(out, hole.patient);
    out << hole.tasks.size();
    for (const auto &task : hole.tasks)
    {
        out << task.projectId << task.rate << task.repeats << task.prolonged << task.calibrationPoint
            << task.calibrationConcentration << int(task.source) << task.lisProjectName << task.lisProjectFile
            << task.lisProjectFingerprint << task.lisProjectInformationFound << task.states.size();
        for (const auto state : task.states) out << int(state);
    }
    const auto &c = hole.calibration;
    out << c.projectId << c.groupToken << c.calibratorBatch << c.reagentBatchGroup << c.autoDilution
        << c.pointCount << c.repeatCount << c.baselineConcentration << c.rates << c.concentrations << c.holes;
    const auto &q = hole.quality.selected;
    out << hole.quality.slotToken << hole.quality.changeToken << q.datasetId << q.controlId << q.batchId
        << q.controlName << q.batchName << q.sampleId << q.projects.size();
    for (const auto &project : q.projects) out << project.associationId << project.projectId;
    return digest(bytes);
}

QString holeLisDigest(const SampleHole &hole)
{
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly); out.setVersion(QDataStream::Qt_5_12);
    out << int(hole.lisPhase) << hole.lisControlId << hole.lisStartedAt << hole.lisIntervalMsecs;
    return digest(bytes);
}

QString rackInputDigest(const SampleRack &rack)
{
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly); out.setVersion(QDataStream::Qt_5_12);
    out << rack.id << rack.name << rack.emergency << rack.holes.size();
    for (const auto &hole : rack.holes) out << holeTaskDigest(hole) << holeLisDigest(hole);
    // TB剩余时间仅显示，不改变患者/任务或查询身份。
    return digest(bytes);
}

QString projectCacheDigest(const ProjectRecords &projects)
{
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly); out.setVersion(QDataStream::Qt_5_12);
    out << projects.size();
    for (const auto &project : projects)
        out << project.id << project.name << project.originalRoot << project.originalFileName << project.originalFingerprint;
    return digest(bytes);
}

SampleHoleState scalarHoleState(const SampleHole &hole)
{
    // 与sampleHoleState同一优先顺序，只看已存状态/重复标量，不物化重复列表。
    bool any = false, idle = false, error = false, finished = false;
    for (const auto &task : hole.tasks)
    {
        if (task.states.isEmpty())
        {
            if (task.repeats > 0) { any = true; idle = true; }
            continue;
        }
        any = true;
        for (const auto state : task.states)
        {
            if (state == SampleTaskState::Testing) return SampleHoleState::Testing;
            idle = idle || state == SampleTaskState::Idle;
            error = error || state == SampleTaskState::Error;
            finished = finished || state == SampleTaskState::Finished;
        }
    }
    if (!any) return SampleHoleState::None;
    if (idle) return SampleHoleState::Ready;
    if (error) return SampleHoleState::Error;
    if (finished) return SampleHoleState::Finished;
    return SampleHoleState::Ready;
}

Sn100Lis::OriginalLisHoleValue originalLisValue(const QString &root, const QString &instance,
                                               const SampleRack &rack, const SampleHole &hole)
{
    Sn100Lis::OriginalLisHoleValue value;
    value.originalRoot = root; value.rackInstance = instance; value.rack = rack.name; value.hole = hole.number;
    const auto &item = hole.patient;
    value.patient.bedNumber = item.bedNumber; value.patient.name = item.name;
    value.patient.sex = item.sex; value.patient.age = item.age; value.patient.sampleNumber = item.sampleNumber;
    value.patient.barcode = item.barcode; value.patient.registrationTime = item.dateTime;
    value.patient.remark = item.remark; value.patient.checker = item.checker; value.patient.department = item.department;
    value.internalSampleNumber = hole.sampleId; value.barcode = hole.sampleBarcode;
    value.sampleType = hole.sampleType; value.needleType = hole.needleType; value.bottleType = hole.container;
    value.taskState = Sn100Lis::OriginalLisHoleTaskState(int(scalarHoleState(hole)));
    return value;
}

bool originalRows(QSqlDatabase &database, const QString &table, OriginalBusiness::Rows *rows, QString *message)
{
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("select * from ") + table)) return fail(message, query.lastError().text());
    const auto fields = query.record();
    while (query.next())
    {
        OriginalBusiness::Row row;
        for (int column = 0; column < fields.count(); ++column) row.insert(fields.fieldName(column), query.value(column));
        rows->append(row);
    }
    return !query.lastError().isValid() || fail(message, query.lastError().text());
}
}

bool OriginalSampleRepository::refreshProjects(QString *message)
{
    QStringList reagents, cleaners, diluents;
    const QString before = projectCacheDigest(projects_);
    const bool loaded = projectRepository_.load(&projects_, &units_, &reagents, &cleaners, &diluents, message);
    if (loaded && before != projectCacheDigest(projects_)) lisProjectRevision_ = nextLisRevision();
    return loaded;
}

bool OriginalSampleRepository::initialize(const QString &root, QString *message, bool newHoleContainerIsTube,
    OriginalIni::InitializationSourcePtr configurationSource)
{
    initialized_ = false;
    lisInitialized_ = false;
    lisThread_ = nullptr;
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
    if (lisInitialization_ != std::numeric_limits<quint64>::max()) ++lisInitialization_;
    else lisRevisionsAvailable_ = false;
    lisRepositoryInstance_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    lisRackRevisions_.clear(); lisHoleRevisions_.clear();
    for (const auto &rack : snapshot_.racks) noteLisRackPublished(rack);
    lisInitialized_ = lisRevisionsAvailable_;
    lisThread_ = QThread::currentThread();
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
    noteLisRackPublished(rack);
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
    const bool inputChanged = index < 0 || rackInputDigest(snapshot_.racks[index]) != rackInputDigest(rack);
    if (!rack.id)
    {
        if (rack.emergency || nextRackId_ == std::numeric_limits<qint64>::max()) return fail(message, QStringLiteral("急诊架不能重复创建或架编号已耗尽。"));
        rack.id = nextRackId_++;
        snapshot_.racks.append(rack);
    }
    else snapshot_.racks[index] = rack;
    if (inputChanged) noteLisRackPublished(rack);
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
            noteLisRackRemoved(id);
            return true;
        }
    return fail(message, QStringLiteral("样本架已不存在。"));
}
bool OriginalSampleRepository::clearOrdinary(QString *message)
{
    for (const auto &rack : snapshot_.racks) if (!rack.emergency && !editable(rack.id, message)) return false;
    for (int i = snapshot_.racks.size() - 1; i >= 0; --i)
        if (!snapshot_.racks[i].emergency)
        {
            const qint64 id = snapshot_.racks[i].id;
            snapshot_.racks.removeAt(i);
            noteLisRackRemoved(id);
        }
    return true;
}
void OriginalSampleRepository::setRackInFlight(qint64 id, bool inFlight)
{
    const bool changed = inFlight_.contains(id) != inFlight;
    if (inFlight) inFlight_.insert(id); else inFlight_.remove(id);
    if (changed) lisRackRevisions_[id] = nextLisRevision();
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
                            const QString before = holeTaskDigest(hole);
                            auto states = sampleTaskStates(preset);
                            if (task.repeatIndex < 0 || task.repeatIndex >= states.size())
                                return fail(message, QStringLiteral("设备任务对应重复次数已改变，未更新其他任务状态。"));
                            states[task.repeatIndex] = state;
                            preset.states = states;
                            const auto aggregate=sampleHoleState(sampleHoleTaskStates(hole));
                            hole.revise = aggregate==SampleHoleState::Finished || aggregate==SampleHoleState::Error;
                            if (before != holeTaskDigest(hole)) noteLisHolePublished(rack.id, hole.number);
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

quint64 OriginalSampleRepository::nextLisRevision()
{
    if (!lisRevisionsAvailable_ || lisRevision_ == std::numeric_limits<quint64>::max())
    {
        lisRevisionsAvailable_ = false; // 不回绕；不改变手工保存/删除的原返回规则。
        return 0;
    }
    return ++lisRevision_;
}

void OriginalSampleRepository::noteLisRackPublished(const SampleRack &rack)
{
    const quint64 revision = nextLisRevision();
    lisRackRevisions_[rack.id] = revision;
    auto &holes = lisHoleRevisions_[rack.id];
    holes.clear();
    for (const auto &hole : rack.holes) holes.insert(hole.number, revision);
}

void OriginalSampleRepository::noteLisHolePublished(qint64 rackId, int hole)
{
    const quint64 revision = nextLisRevision();
    lisRackRevisions_[rackId] = revision;
    lisHoleRevisions_[rackId][hole] = revision;
}

void OriginalSampleRepository::noteLisRackRemoved(qint64 rackId)
{
    nextLisRevision();
    lisRackRevisions_.remove(rackId);
    lisHoleRevisions_.remove(rackId);
}

bool OriginalSampleRepository::originalLisEditable(qint64 rackId, QString *message) const
{
    // 保留原editable的整架在途/Testing保护，单独按标量判断，避免LIS次数预展开。
    if (inFlight_.contains(rackId))
        return fail(message, QStringLiteral("该样本架有已提交或执行中的任务，LIS未修改原孔。"));
    for (const auto &rack : snapshot_.racks)
        if (rack.id == rackId)
        {
            for (const auto &hole : rack.holes)
                if (scalarHoleState(hole) == SampleHoleState::Testing)
                    return fail(message, QStringLiteral("样本架正在测试，LIS未修改原孔。"));
            return true;
        }
    return fail(message, QStringLiteral("原LIS目标架已不存在，未查找同名新架替代。"));
}

bool OriginalSampleRepository::originalLisProjectSource(QString *sourceDigest, QStringList *files, QString *message) const
{
    if (!sourceDigest || !files || !lisInitialized_ || !lisRevisionsAvailable_ ||
        lisThread_ != QThread::currentThread() || !configurationSource_ || !configurationSource_->current(root_, message))
        return fail(message, QStringLiteral("LIS原仓库线程、初始化或同次INI来源已失效。"));
    QDir directory(QDir(root_).filePath(QStringLiteral("project")));
    if (!directory.exists() || QFileInfo(directory.absolutePath()).isSymLink())
        return fail(message, QStringLiteral("原项目目录缺失或不是本次实际目录。"));
    QStringList selected;
    const auto names = directory.entryList({QStringLiteral("*.db")}, QDir::Files | QDir::NoSymLinks, QDir::Name | QDir::IgnoreCase);
    for (const auto &name : names)
        if (!name.contains(QStringLiteral("formula"), Qt::CaseSensitive)) selected.append(name);
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly); out.setVersion(QDataStream::Qt_5_12);
    out << QStringLiteral("SN100-LIS-project-source-v1") << configurationSource_->versions() << selected;
    QMap<QString, QString> versions;
    for (const auto &name : selected)
    {
        const QString fingerprint = OriginalProjectRepository::databaseFingerprint(directory.filePath(name), message);
        if (fingerprint.isEmpty()) return false;
        versions.insert(name, fingerprint);
        out << name << fingerprint;
    }
    QStringList afterFiles;
    for (const auto &name : directory.entryList({QStringLiteral("*.db")}, QDir::Files | QDir::NoSymLinks, QDir::Name | QDir::IgnoreCase))
        if (!name.contains(QStringLiteral("formula"), Qt::CaseSensitive)) afterFiles.append(name);
    if (afterFiles != selected) return fail(message, QStringLiteral("读取原LIS项目来源期间文件集合改变。"));
    for (auto version = versions.cbegin(); version != versions.cend(); ++version)
        if (version.value() != OriginalProjectRepository::databaseFingerprint(directory.filePath(version.key()), message))
            return fail(message, QStringLiteral("读取原LIS项目来源期间文件版本改变。"));
    if (!configurationSource_->current(root_, message)) return false;
    *sourceDigest = digest(bytes); *files = selected;
    return true;
}

bool OriginalSampleRepository::captureOriginalLisMappings(OriginalLisMappingSnapshot *snapshot, QString *message) const
{
    if (!snapshot) return fail(message, QStringLiteral("没有原LIS映射输出位置。"));
    *snapshot = OriginalLisMappingSnapshot();
    QString beforeSource; QStringList files;
    if (!originalLisProjectSource(&beforeSource, &files, message)) return false;
    struct Entry { int order = 0; Sn100Lis::OriginalLisMappingProject project; };
    QList<Entry> entries;
    for (const auto &name : files)
    {
        const QString relative = QStringLiteral("project/") + name;
        const QString fingerprint = OriginalProjectRepository::databaseFingerprint(QDir(root_).filePath(relative), message);
        if (fingerprint.isEmpty()) return false;
        Entry entry;
        {
            // 不调用ProjectRepository::load/recoverPending；只开本文件的只读连接。
            OriginalBusiness::Database database;
            if (!database.open(root_, relative, false, message)) return false;
            OriginalBusiness::Rows baseRows, resultRows, rateRows;
            if (!originalRows(database.db, QStringLiteral("project_base"), &baseRows, message) ||
                !originalRows(database.db, QStringLiteral("project_results"), &resultRows, message) ||
                !originalRows(database.db, QStringLiteral("project_sample_rate"), &rateRows, message)) return false;
            QMap<QString, QString> base;
            for (const auto &row : baseRows)
                base.insert(row.value(QStringLiteral("name")).toString(), row.value(QStringLiteral("value")).toString());
            entry.order = base.value(QStringLiteral("xuhao")).toInt();
            auto &project = entry.project;
            project.projectId = entry.order;
            project.name = base.value(QStringLiteral("project_en_name"));
            project.informationName = base.value(QStringLiteral("project_en_name"));
            project.originalFile = name; project.originalFingerprint = fingerprint;
            for (const auto &row : resultRows) project.resultLisCodes.append(row.value(QStringLiteral("lis_code")).toString());
            for (const auto &row : rateRows)
            {
                Sn100Lis::OriginalLisMappingRate rate;
                rate.rate = row.value(QStringLiteral("sample_rate")).toString();
                rate.defaultOption = row.value(QStringLiteral("sample_default_option")).toString().toInt();
                project.sampleRates.append(rate);
            }
        }
        if (fingerprint != OriginalProjectRepository::databaseFingerprint(QDir(root_).filePath(relative), message))
            return fail(message, QStringLiteral("读取原LIS映射期间项目文件改变，未采用部分快照。"));
        entries.append(entry);
    }
    // 原第一遍std::sort不稳定；不加同号去重或新次序，第二遍才按sendSequence稳定排序。
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.order < b.order; });
    const auto &normal = configurationSource_->normal();
    const QString wanted = QStringLiteral("sendConfig/sendSequence");
    QString actual = wanted;
    if (!normal.values.contains(wanted))
    {
        QStringList matches;
        for (auto row = normal.values.cbegin(); row != normal.values.cend(); ++row)
            if (row.key().compare(wanted, Qt::CaseInsensitive) == 0) matches.append(row.key());
        if (matches.size() > 1) return fail(message, QStringLiteral("原sendSequence键大小写来源有歧义。"));
        if (matches.size() == 1) actual = matches.first();
    }
    const auto sequence = normal.values.value(actual, QStringList()).toStringList();
    const auto rank = [&sequence](const Entry &entry)
    {
        for (int i = 0; i < sequence.size(); ++i)
            if (entry.project.name.startsWith(sequence[i], Qt::CaseInsensitive)) return i;
        return std::numeric_limits<int>::max();
    };
    if (!sequence.isEmpty())
        std::stable_sort(entries.begin(), entries.end(), [&rank](const Entry &a, const Entry &b) { return rank(a) < rank(b); });
    Sn100Lis::OriginalLisMappingProjects result;
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly); out.setVersion(QDataStream::Qt_5_12);
    out << beforeSource << entries.size();
    for (const auto &entry : entries)
    {
        const auto &p = entry.project;
        result.append(p);
        out << p.projectId << p.name << p.informationName << p.originalFile << p.originalFingerprint << p.resultLisCodes << p.sampleRates.size();
        for (const auto &rate : p.sampleRates) out << rate.rate << rate.defaultOption;
    }
    QString afterSource; QStringList afterFiles;
    if (!originalLisProjectSource(&afterSource, &afterFiles, message) || afterSource != beforeSource || afterFiles != files)
        return fail(message, QStringLiteral("原LIS项目/INI来源在读取期间改变，未用新配置续签旧输入。"));
    OriginalLisMappingSnapshot complete;
    complete.projectSourceDigest_ = beforeSource; complete.digest_ = digest(bytes);
    complete.projects_ = std::move(result); complete.valid_ = true;
    *snapshot = std::move(complete);
    return true;
}

bool OriginalSampleRepository::captureOriginalLisHole(qint64 rackId, int number, OriginalLisSampleTarget *target,
    Sn100Lis::OriginalLisHoleValue *oldValue, QString *message) const
{
    if (!target || !oldValue) return fail(message, QStringLiteral("没有原LIS孔值输出位置。"));
    QString source; QStringList files;
    if (!originalLisProjectSource(&source, &files, message)) return false;
    for (const auto &rack : snapshot_.racks)
        if (rack.id == rackId)
            for (const auto &hole : rack.holes)
                if (hole.number == number)
                {
                    OriginalLisSampleTarget captured;
                    captured.root = root_; captured.repositoryInstance = lisRepositoryInstance_; captured.initialization = lisInitialization_;
                    captured.rackId = rackId; captured.rack = rack.name; captured.hole = number;
                    captured.rackRevision = lisRackRevisions_.value(rackId);
                    captured.holeRevision = lisHoleRevisions_.value(rackId).value(number);
                    captured.projectRevision = lisProjectRevision_;
                    captured.taskInputDigest = holeTaskDigest(hole); captured.lisContextDigest = holeLisDigest(hole);
                    captured.projectSourceDigest = source;
                    if (!captured.rackRevision || !captured.holeRevision || !captured.initialization)
                        return fail(message, QStringLiteral("原LIS架孔没有实际发布修订，未制造默认身份。"));
                    *target = captured; *oldValue = originalLisValue(root_, lisRepositoryInstance_, rack, hole);
                    return true;
                }
    return fail(message, QStringLiteral("原LIS目标架孔已不存在，未选择另一个孔。"));
}

bool OriginalSampleRepository::matchOriginalLisTarget(const OriginalLisSampleTarget &target,
                                                       int *rackIndex, int *holeIndex, QString *message) const
{
    OriginalLisSampleTarget current; Sn100Lis::OriginalLisHoleValue ignored;
    if (!captureOriginalLisHole(target.rackId, target.hole, &current, &ignored, message)) return false;
    if (current.root != target.root || current.repositoryInstance != target.repositoryInstance ||
        current.initialization != target.initialization || current.rack != target.rack ||
        current.rackRevision != target.rackRevision || current.holeRevision != target.holeRevision ||
        current.projectRevision != target.projectRevision || current.taskInputDigest != target.taskInputDigest ||
        current.lisContextDigest != target.lisContextDigest || current.projectSourceDigest != target.projectSourceDigest)
        return fail(message, QStringLiteral("LIS原仓库/架孔或项目输入已改变，未拿最新值给旧请求续签。"));
    if (!originalLisEditable(target.rackId, message)) return false;
    for (int r = 0; r < snapshot_.racks.size(); ++r)
        if (snapshot_.racks[r].id == target.rackId)
            for (int h = 0; h < snapshot_.racks[r].holes.size(); ++h)
                if (snapshot_.racks[r].holes[h].number == target.hole)
                { *rackIndex = r; *holeIndex = h; return true; }
    return false;
}

bool OriginalSampleRepository::publishOriginalLisHole(OriginalLisSampleTarget target, SampleHole after,
    OriginalLisSampleChangeKind kind, bool queryRequired, OriginalLisSampleChange *result, QString *message)
{
    if (!result) return fail(message, QStringLiteral("没有原LIS内存变化输出位置。"));
    *result = OriginalLisSampleChange();
    int r = 0, h = 0;
    if (!matchOriginalLisTarget(target, &r, &h, message)) return false;
    if (lisRevision_ == std::numeric_limits<quint64>::max())
        return fail(message, QStringLiteral("LIS本地修订号耗尽，未回绕或修改原孔。"));
    // 所有拷贝/摘要先准备；发布只交换容器，不调用SQL、信号、回调或事件循环。
    auto racks = snapshot_.racks;
    auto rackRevisions = lisRackRevisions_;
    auto holeRevisions = lisHoleRevisions_;
    const quint64 revision = lisRevision_ + 1;
    racks[r].holes[h] = std::move(after);
    rackRevisions[target.rackId] = revision; holeRevisions[target.rackId][target.hole] = revision;
    OriginalLisSampleChange applied;
    applied.kind = kind;
    applied.memoryApplied = true; applied.queryRequired = queryRequired; applied.before = target; applied.after = target;
    applied.after.rackRevision = revision; applied.after.holeRevision = revision;
    applied.after.taskInputDigest = holeTaskDigest(racks[r].holes[h]);
    applied.after.lisContextDigest = holeLisDigest(racks[r].holes[h]);
    snapshot_.racks.swap(racks); lisRackRevisions_.swap(rackRevisions); lisHoleRevisions_.swap(holeRevisions);
    lisRevision_ = revision;
    *result = applied;
    return true;
}

bool OriginalSampleRepository::initializeOriginalLisScan(OriginalLisSampleTarget target, QString barcode,
    QDateTime startedAt, OriginalLisSampleChange *result, QString *message)
{
    if (result) *result = OriginalLisSampleChange();
    int r = 0, h = 0;
    if (!matchOriginalLisTarget(target, &r, &h, message)) return false;
    SampleHole after = snapshot_.racks[r].holes[h];
    after.sampleBarcode = std::move(barcode); after.lisControlId = QStringLiteral(""); after.lisPhase = SampleLisPhase::None;
    after.patient = SamplePatientItem(); after.lisStartedAt = std::move(startedAt);
    return publishOriginalLisHole(target, std::move(after), OriginalLisSampleChangeKind::ScanInitialized, false, result, message);
}

bool OriginalSampleRepository::beginOriginalLisQuery(OriginalLisSampleTarget target, QDateTime startedAt,
    OriginalLisSampleChange *result, QString *message)
{
    if (result) *result = OriginalLisSampleChange();
    int r = 0, h = 0;
    if (!matchOriginalLisTarget(target, &r, &h, message)) return false;
    SampleHole after = snapshot_.racks[r].holes[h];
    if (after.lisPhase != SampleLisPhase::None) return fail(message, QStringLiteral("原孔不处于LIS_NONE，未重复开始查询。"));
    const QStringList invalid = {QStringLiteral("None"), QStringLiteral("none"), QStringLiteral("empty"),
        QStringLiteral("Empty"), QStringLiteral("Null"), QStringLiteral("")};
    const bool queryRequired = !invalid.contains(after.sampleBarcode, Qt::CaseInsensitive);
    if (queryRequired) { after.lisPhase = SampleLisPhase::T1; after.lisStartedAt = std::move(startedAt); }
    else after.lisPhase = SampleLisPhase::Max;
    return publishOriginalLisHole(target, std::move(after), queryRequired ? OriginalLisSampleChangeKind::QueryStarted :
        OriginalLisSampleChangeKind::InvalidBarcodeEnded, queryRequired, result, message);
}

bool OriginalSampleRepository::bindOriginalLisQuery(OriginalLisSampleTarget target, QString controlId,
    OriginalLisSampleChange *result, QString *message)
{
    if (result) *result = OriginalLisSampleChange();
    int r = 0, h = 0;
    if (!matchOriginalLisTarget(target, &r, &h, message)) return false;
    SampleHole after = snapshot_.racks[r].holes[h];
    if (after.lisPhase != SampleLisPhase::T1) return fail(message, QStringLiteral("原孔未完成本次T1初始化，未绑定查询控制号。"));
    after.lisControlId = std::move(controlId); after.patient = SamplePatientItem();
    return publishOriginalLisHole(target, std::move(after), OriginalLisSampleChangeKind::QueryBound, true, result, message);
}

bool OriginalSampleRepository::markOriginalLisReceiving(OriginalLisSampleTarget target, QString controlId,
    OriginalLisSampleChange *result, QString *message)
{
    if (result) *result = OriginalLisSampleChange();
    int r = 0, h = 0;
    if (!matchOriginalLisTarget(target, &r, &h, message)) return false;
    SampleHole after = snapshot_.racks[r].holes[h];
    if (after.lisControlId != controlId || after.lisPhase == SampleLisPhase::R1 || after.lisPhase == SampleLisPhase::Max)
        return fail(message, QStringLiteral("LIS控制号不属原孔，或原孔已接收/结束，未重复认领。"));
    after.lisPhase = SampleLisPhase::R1;
    return publishOriginalLisHole(target, std::move(after), OriginalLisSampleChangeKind::ReplyReceiving, false, result, message);
}

bool OriginalSampleRepository::prepareOriginalLisApplication(const OriginalLisSampleTarget &target,
    const Sn100Lis::OriginalLisDsrFields &decoded, OriginalLisPreparedSampleUpdate *prepared, QString *message) const
{
    if (!prepared) return fail(message, QStringLiteral("没有LIS准备值输出位置。"));
    *prepared = OriginalLisPreparedSampleUpdate();
    int r = 0, h = 0;
    if (!matchOriginalLisTarget(target, &r, &h, message)) return false;
    const auto &rack = snapshot_.racks[r]; const auto &before = rack.holes[h];
    if (before.lisPhase != SampleLisPhase::R1 || before.lisControlId != decoded.header.controlId)
        return fail(message, QStringLiteral("原LIS孔尚未认领本次R1，未把Prepared值当实际接收资格。"));
    OriginalLisMappingSnapshot mappings;
    if (!captureOriginalLisMappings(&mappings, message)) return false;
    const auto plan = Sn100Lis::buildOriginalLisApplicationPlan(decoded, originalLisValue(root_, lisRepositoryInstance_, rack, before), mappings.projects());
    if (plan.state != Sn100Lis::OriginalLisPlanState::Prepared)
        return fail(message, plan.error.field + QStringLiteral(": ") + plan.error.detail);
    SampleHole after = before;
    const auto &patient = plan.after.patient;
    const auto text = [](const QString &value) { return value.isNull() ? QStringLiteral("") : value; };
    after.patient.bedNumber = text(patient.bedNumber); after.patient.name = text(patient.name);
    after.patient.sex = patient.sex; after.patient.age = patient.age; after.patient.sampleNumber = patient.sampleNumber;
    after.patient.barcode = text(patient.barcode); after.patient.dateTime = patient.registrationTime;
    after.patient.remark = text(patient.remark); after.patient.checker = text(patient.checker); after.patient.department = text(patient.department);
    if (plan.holeAssigned.sampleType) after.sampleType = plan.after.sampleType;
    if (plan.holeAssigned.needleType) after.needleType = plan.after.needleType;
    if (plan.holeAssigned.bottleType) after.container = plan.after.bottleType;
    if (plan.holeAssigned.internalSampleNumber) after.sampleId = plan.after.internalSampleNumber;
    if (plan.holeAssigned.barcode) after.sampleBarcode = plan.after.barcode;
    after.tasks.clear();
    for (auto task = plan.tasks.cbegin(); task != plan.tasks.cend(); ++task)
    {
        SampleTask value;
        value.projectId = task->projectId; value.rate = task->defaultRate; value.repeats = task->repeats;
        value.source = SampleTaskSource::OriginalLisDownload; value.lisProjectName = task->projectName;
        value.lisProjectInformationFound = task->projectInformationFound;
        value.lisProjectFile = task->originalFile; value.lisProjectFingerprint = task->originalFingerprint;
        after.tasks.append(value); // 保原QString项目名顺序及重复标量，不物化状态、不夹99。
    }
    after.lisPhase = SampleLisPhase::Max;
    // 读取和A9准备之后再核原目标；失败保留已发生R1，不写半个患者或提前MAX。
    int sameRack = 0, sameHole = 0;
    if (!matchOriginalLisTarget(target, &sameRack, &sameHole, message)) return false;
    OriginalLisPreparedSampleUpdate complete;
    complete.before_ = target; complete.after_ = std::move(after); complete.mappingDigest_ = mappings.digest(); complete.prepared_ = true;
    *prepared = std::move(complete);
    return true;
}

bool OriginalSampleRepository::applyOriginalLisApplication(const OriginalLisPreparedSampleUpdate &prepared,
    OriginalLisSampleChange *result, QString *message)
{
    if (result) *result = OriginalLisSampleChange();
    if (!prepared.prepared_) return fail(message, QStringLiteral("没有由本仓库完整准备的LIS更新，未保存默认空值。"));
    return publishOriginalLisHole(prepared.before_, prepared.after_, OriginalLisSampleChangeKind::PatientTasksApplied, false, result, message);
}
