#include "original_sample_repository.h"
#include "original_files/original_business_database.h"
#include "original_files/original_ini.h"
#include <QDir>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
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

bool OriginalSampleRepository::initialize(const QString &root, QString *message)
{
    if (!OriginalBusiness::workingRoot(root, message) || !projectRepository_.initialize(root, {}, message)) return false;
    OriginalIni::Settings theme;
    if (!OriginalIni::read(QDir(root).filePath(QStringLiteral("theme_config.ini")), &theme, message)) return false;
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
        emergency.holes.append(hole);
    }
    snapshot_.racks.append(emergency);
    nextRackId_ = 2;
    inFlight_.clear();
    return load(&snapshot_, message);
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
        // 原质控库尚未连入该模式时不能把早期质控身份当原版的质控批号使用。
        if (hole.sampleType != 0) return fail(message, QStringLiteral("原质控样本批号尚未读取完成；请先完成原质控数据接入。"));
        QSet<qint64> seen;
        for (const auto &task : hole.tasks)
        {
            const auto *project = findProject(projects_, task.projectId);
            if (!project || !project->enabled || seen.contains(task.projectId) || task.repeats < 1 || task.repeats > 99 ||
                (!task.states.isEmpty() && task.states.size() != task.repeats))
                return fail(message, QStringLiteral("样本引用项目已失效、项目重复或重复次数不合法。"));
            seen.insert(task.projectId);
            for (const auto state : sampleTaskStates(task))
                if (state != SampleTaskState::Idle && state != SampleTaskState::Reset && state != SampleTaskState::Testing &&
                    state != SampleTaskState::Finished && state != SampleTaskState::Error)
                    return fail(message, QStringLiteral("样本任务状态未识别，未保存草稿。"));
            bool rate = false;
            for (const auto &option : project->sampleRates) if (option.sampleRate == task.rate && option.sampleOption) rate = true;
            if (!rate) return fail(message, QStringLiteral("样本所选稀释方案已不存在或未启用。"));
        }
    }
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
                        if (preset.projectId == task.project.id)
                        {
                            auto states = sampleTaskStates(preset);
                            if (task.repeatIndex < 0 || task.repeatIndex >= states.size())
                                return fail(message, QStringLiteral("设备任务对应重复次数已改变，未更新其他任务状态。"));
                            states[task.repeatIndex] = state;
                            preset.states = states;
                            hole.revise = false;
                            return true;
                        }
    return fail(message, QStringLiteral("设备任务对应的样本架/孔位/项目已不存在，原结果保留待核。"));
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
