#include "original_reagent_editor.h"
#include "original_files/original_ini.h"
#include "projects/original_project_copy.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace OriginalBusiness;
namespace
{
const QString informationFile = QStringLiteral("db/userSet/reagent_Information.db");
const QString bottleFile = QStringLiteral("db/systemInfo/reagent_Bottle_Type.db");
const QString settingFile = QStringLiteral("db/userSet/reagent_Setting.db");
const QString isiFile = QStringLiteral("db/userSet/isi_Value.db");
const QStringList storedTypes{" ","r_reagent","s_ibs","cleaner","s_control","s_calibrator","s_Factor"};
const QStringList shownTypes{QStringLiteral("无"),QStringLiteral("试剂"),QStringLiteral("缓冲稀释液"),QStringLiteral("清洗液"),
                             QStringLiteral("质控品"),QStringLiteral("校准品"),QStringLiteral("因子血浆")};
bool fail(QString *message, const QString &text)
{
    if (message) *message = text;
    return false;
}
const Row *byId(const Rows &rows, qint64 id)
{
    for (const auto &row : rows) if (row.value("id").toLongLong()==id) return &row;
    return nullptr;
}
qint64 idByName(const Rows &rows, const QString &key, const QString &name)
{
    for (const auto &row : rows) if (row.value(key).toString()==name) return row.value("id").toLongLong();
    return 0;
}
bool nameValid(const QString &name, int maximum=64)
{
    if (name.isEmpty() || name!=name.trimmed() || name.size()>maximum) return false;
    for (const auto c : name) if (c.unicode()<32 || c=='[' || c==']' || c=='|' || c=='/' || c=='\\') return false;
    return true;
}
bool allRows(Database &database, const QString &table, Rows *rows, QString *message)
{
    QSqlQuery query(database.db);
    if (!query.exec(QStringLiteral("SELECT * FROM \"%1\" ORDER BY id").arg(table))) return fail(message,query.lastError().text());
    const auto columns=query.record();
    rows->clear();
    while (query.next())
    {
        if (rows->size()>=100000) return fail(message,QStringLiteral("原试剂资料超过10万行，未截断。"));
        Row row;
        for (int i=0;i<columns.count();++i) row.insert(columns.fieldName(i),query.value(i));
        rows->append(row);
    }
    return !query.lastError().isValid() || fail(message,query.lastError().text());
}
void changedInteger(QVariantMap *values, const Row *before, const QString &key, int value)
{
    values->insert(key,before && before->value(key).toInt()==value ? before->value(key) : QVariant(value));
}
QString displayedType(const QString &stored)
{
    const int index=storedTypes.indexOf(stored);
    return index<0 ? stored : shownTypes[index];
}

ReagentTestCount unknownTestCount(const QString &reason)
{
    ReagentTestCount result;
    result.reason=reason;
    return result;
}

bool finiteNumber(const QVariant &value, double *number)
{
    bool ok=false;
    *number=value.toString().toDouble(&ok);
    return ok && std::isfinite(*number);
}

QString firstDisplayUse(const ProjectRecords &projects, const QString &reagentName)
{
    // 原getReagentTests_Impl只看每个项目的首个同名步骤；空值才继续下一个项目。
    for (const auto &project : projects)
        for (const auto &step : project.advanced.reagents)
            if (step.value(QStringLiteral("reagent_name"))==reagentName)
            {
                const QString amount=step.value(QStringLiteral("reagent_amount_ul"));
                if (!amount.isEmpty()) return amount;
                break;
            }
    return QString();
}

ReagentTestCount displayHoleTests(const Row &hole, const OriginalReagentState &state,
                                 const ProjectRecords &projects)
{
    const QString name=hole.value("reagent_name").toString();
    const Row *definition=nullptr;
    for (const auto &row : state.reagents)
        if (row.value("reagent_name").toString()==name) { definition=&row; break; }
    if (name.isEmpty() || name==QStringLiteral("none") || !definition)
        return unknownTestCount(QStringLiteral("未设置试剂或找不到对应的原试剂资料。"));
    double volume=0, minimum=0, use=0;
    if (!finiteNumber(hole.value("amount_uL"),&volume) || !finiteNumber(definition->value("out_ul"),&minimum) ||
        !finiteNumber(firstDisplayUse(projects,name),&use) || use<=0)
        return unknownTestCount(QStringLiteral("当前液量、瓶底保留量或首个项目用量无效，无法计算次数。"));

    // 对应原initPanel的显示加载口径：先按首个同名瓶型限制上限；原构造器默认5000µL。
    // 只调整计算用的副本，amount_uL和used_ul原值均不改写。
    double maximum=5000.0;
    for (const auto &bottle : state.bottles)
        if (bottle.value("bottle_type_name").toString()==hole.value("bottle_type").toString())
        {
            if (!finiteNumber(bottle.value("max_amount_ul"),&maximum))
                return unknownTestCount(QStringLiteral("原瓶型最大容量无效，无法确定显示液量。"));
            break;
        }
    volume=qMin(volume,maximum);
    const double calculated=std::floor((volume-minimum)/use);
    if (!std::isfinite(calculated) || calculated>std::numeric_limits<int>::max())
        return unknownTestCount(QStringLiteral("原显示次数超出整数范围，未截断或显示为0。"));
    ReagentTestCount result;
    result.available=true;
    result.tests=calculated<=0 ? 0 : static_cast<int>(calculated);
    return result;
}

bool orderedOriginalProjects(const QString &root, const ProjectRecords &records, ProjectRecords *projects,
                             const OriginalIni::InitializationSourcePtr &source, QString *message)
{
    if (!source || !source->current(root,message)) return false;
    *projects=records;
    QStringList sequence;
    QString actualKey;
    if (!OriginalIni::resolveKey(source->normal(),QStringLiteral("sendConfig/sendSequence"),&actualKey,message)) return false;
    if (!actualKey.isEmpty()) sequence=source->normal().values.value(actualKey).toStringList();
    // 原loadAllProject先按xuhao，再按配置中的项目名前缀稳定排序；未启用项目也保留。
    if (!sequence.isEmpty())
    {
        const auto rank=[&sequence](const ProjectRecord &project)
        {
            for (int i=0;i<sequence.size();++i)
                if (project.name.startsWith(sequence[i],Qt::CaseInsensitive)) return i;
            return std::numeric_limits<int>::max();
        };
        std::stable_sort(projects->begin(),projects->end(),[&rank](const ProjectRecord &a,const ProjectRecord &b)
        { return rank(a)<rank(b); });
    }
    return source->current(root,message);
}

bool singleProbeNeedles(const QString &root, const ProjectRecords &orderedProjects, const QString &reagent,
                       OriginalReagentProbe::Clock::time_point deadline, QMap<QString,int> *needles, QString *message)
{
    if (!needles) return fail(message,QStringLiteral("单孔探测针号输出为空，未准备命令。"));
    const auto inTime=[&]()
    {
        return OriginalReagentProbe::Clock::now()<deadline ||
            fail(message,QStringLiteral("单孔探测读取原项目步骤时已过准备期限，未发布针号或发送。"));
    };
    QMap<QString,int> selected;
    for (const auto &project : orderedProjects)
    {
        if (!inTime()) return false;
        // 原loadAllProject只排除文件名含小写formula的项目；来源集合和版本仍完整核对。
        if (project.originalFileName.contains(QStringLiteral("formula"),Qt::CaseSensitive)) continue;
        Database database;
        if (!database.open(root,QStringLiteral("project/")+project.originalFileName,false,message) || !inTime()) return false;
        QSqlQuery query(database.db);
        // 原loadProject/getData无ORDER BY：保留SELECT *实际返回顺序，不能改成xuhao或rowid排序。
        if (!query.exec(QStringLiteral("SELECT * FROM project_reagent")))
            return fail(message,QStringLiteral("单孔探测无法完整读取原项目试剂步骤：")+query.lastError().text());
        const auto columns=query.record();
        int nameColumn=-1, needleColumn=-1;
        for (int column=0;column<columns.count();++column)
        {
            if (columns.fieldName(column)==QStringLiteral("reagent_name")) nameColumn=column;
            if (columns.fieldName(column)==QStringLiteral("needle_select")) needleColumn=column;
        }
        if (nameColumn<0 || needleColumn<0)
            return fail(message,QStringLiteral("单孔探测原项目缺少准确的试剂名或针号字段，未采用部分步骤。"));
        int rows=0;
        while (query.next())
        {
            if (!inTime()) return false;
            if (rows>=4096) return fail(message,QStringLiteral("单孔探测原项目步骤超过4096行，未截断或发布针号。"));
            ++rows;
            if (query.value(nameColumn).toString()==reagent)
                // 原loadProject先把原串1译为左针、其他译为右针；dectLV按后出现者覆盖。
                selected[reagent]=query.value(needleColumn).toString()==QStringLiteral("1") ? 1 : 2;
        }
        if (query.lastError().isValid())
            return fail(message,QStringLiteral("单孔探测原项目步骤未读完，未采用部分针号：")+query.lastError().text());
        if (!inTime()) return false;
    }
    if (!inTime()) return false;
    *needles=selected;
    return true;
}

bool probeProjectFilesMatch(const QString &root, const ProjectRecords &projects,
                            const QMap<QString,QString> &versions, QString *message)
{
    QSet<QString> expected, versionFiles, actual;
    for (const auto &project : projects)
    {
        if (project.originalFileName.isEmpty() || expected.contains(project.originalFileName))
            return fail(message,QStringLiteral("探测缓存没有唯一原项目文件来源。"));
        expected.insert(project.originalFileName);
    }
    for (auto it=versions.cbegin();it!=versions.cend();++it)
        if (it.key().startsWith(QStringLiteral("project/"))) versionFiles.insert(it.key().mid(8));
    // 与OriginalProjectRepository::readProjects相同：不过滤N的formula/128文件范围。
    const QDir directory(QDir(root).filePath(QStringLiteral("project")));
    const auto entries=directory.entryInfoList(QDir::Files | QDir::NoDotAndDotDot | QDir::System,QDir::Name);
    for (const auto &entry : entries)
        if (entry.fileName().endsWith(QStringLiteral(".db"),Qt::CaseInsensitive)) actual.insert(entry.fileName());
    return (expected==versionFiles && versionFiles==actual) ||
        fail(message,QStringLiteral("原项目.db集合或缓存版本集合已变化，请重读后准备；未仅核旧文件或给旧缓存续签。"));
}

QList<int> operationPanelHoles(const OriginalReagentState &state)
{
    const QRegularExpression names(state.settingTable==QStringLiteral("reagent_setting_6600")
        ? QStringLiteral("^([A-F]_[1-7]|SD_[1-4])$") : QStringLiteral("^(R_([1-9]|1[0-9]|20)|SD_[1-4])$"));
    QList<int> result;
    for (int index=0;index<state.holes.size();++index)
        if (names.match(state.holes[index].value("hole_name").toString()).hasMatch()) result.append(index);
    return result;
}

using HoleChanges = QMap<qint64,QVariantMap>;

void changeOperationHole(OriginalReagentState *state, int index, const QString &field,
                         const QVariant &value, HoleChanges *changes)
{
    auto &hole=state->holes[index];
    hole[field]=value;
    (*changes)[hole.value("id").toLongLong()].insert(field,value);
}

bool arrangeOperationPriority(OriginalReagentState *state, const QList<int> &panel,
                              const QString &area, const QString &reagentName,
                              HoleChanges *changes, QString *message)
{
    QList<int> matching;
    for (int index : panel)
    {
        const auto &hole=state->holes[index];
        if (hole.value("state").toString()==QStringLiteral("r_none") ||
            hole.value("area").toString()!=area || hole.value("reagent_name").toString()!=reagentName) continue;
        bool ok=false;
        const int priority=hole.value("priority").toString().toInt(&ok);
        if (!ok || priority<0) return fail(message,QStringLiteral("原孔位优先级不是有效非负整数，未重新排列。"));
        matching.append(index);
    }
    // 沿用原arrangePriority的比较规则；同优先级不另外发明按孔号先后的业务规则。
    std::sort(matching.begin(),matching.end(),[state](int left,int right)
    { return state->holes[left].value("priority").toInt()<state->holes[right].value("priority").toInt(); });
    int priority=1;
    for (int index : matching)
    {
        if (state->holes[index].value("priority").toInt()!=priority)
            changeOperationHole(state,index,QStringLiteral("priority"),priority,changes);
        ++priority;
    }
    return true;
}

void loadDisplayTestCounts(const QString &root, const OriginalReagentState &state,
                          const ProjectRecords &records, ReagentSnapshot *snapshot,
                          const OriginalIni::InitializationSourcePtr &source)
{
    ProjectRecords projects;
    QString orderError, message;
    if (!orderedOriginalProjects(root,records,&projects,source,&message))
        orderError=QStringLiteral("无法核对原项目顺序：")+message;
    for (const auto &project : projects) snapshot->originalProjectOrder.append(project.name);

    QMap<QString,ReagentTestCount> byReagent;
    const QRegularExpression visibleHoles(state.settingTable==QStringLiteral("reagent_setting_6600")
        ? QStringLiteral("^([A-F]_[1-7]|SD_[1-4])$") : QStringLiteral("^(R_([1-9]|1[0-9]|20)|SD_[1-4])$"));
    for (const auto &hole : state.holes)
    {
        // 原initPanel只汇总当前20位或42位面板上实际存在的孔；不把表中额外行当作实瓶。
        if (!visibleHoles.match(hole.value("hole_name").toString()).hasMatch()) continue;
        const ReagentTestCount count=orderError.isEmpty() ? displayHoleTests(hole,state,projects) : unknownTestCount(orderError);
        snapshot->holeTestCounts.insert(hole.value("hole_name").toString().toUpper(),count);
        const QString name=hole.value("reagent_name").toString();
        if (hole.value("reagent_type").toString()!=QStringLiteral("r_reagent") || name.isEmpty() || name==QStringLiteral("none")) continue;
        if (!byReagent.contains(name)) byReagent.insert(name,count);
        else
        {
            auto &total=byReagent[name];
            if (!count.available) total=count;
            else if (total.available)
            {
                if (count.tests>std::numeric_limits<int>::max()-total.tests)
                    total=unknownTestCount(QStringLiteral("同名试剂的总次数超出整数范围。"));
                else total.tests+=count.tests;
            }
        }
    }
    for (const auto &project : projects)
    {
        bool allPresent=!project.advanced.reagents.isEmpty();
        ReagentTestCount total;
        total.available=true;
        total.tests=std::numeric_limits<int>::max();
        for (const auto &step : project.advanced.reagents)
        {
            const QString name=step.value(QStringLiteral("reagent_name"));
            if (!byReagent.contains(name)) { allPresent=false; break; }
            const ReagentTestCount count=byReagent.value(name);
            if (!count.available) total=count;
            else if (total.available) total.tests=qMin(total.tests,count.tests);
        }
        if (allPresent) snapshot->projectTestCounts.append(qMakePair(project.name,total));
    }
    const bool changed=orderError.isEmpty() && (!source || !source->current(root,&message));
    if (changed)
    {
        const auto unknown=unknownTestCount(QStringLiteral("读取期间原项目顺序配置变化，请重新读取次数。"));
        for (auto it=snapshot->holeTestCounts.begin();it!=snapshot->holeTestCounts.end();++it) it.value()=unknown;
        for (auto &project : snapshot->projectTestCounts) project.second=unknown;
    }
}

bool attachIsi(Database &database, const QString &root, QString *message)
{
    // 与主库一致，附加库也必须已存在，不能在检查后被移走时另建空库。
    const QString uri=QUrl::fromLocalFile(QDir(root).filePath(isiFile)).toString(QUrl::FullyEncoded)+QStringLiteral("?mode=rw");
    if (!database.execute("ATTACH DATABASE ? AS isi",{uri},message)) return false;
    for (const auto &schema : {QStringLiteral("main"),QStringLiteral("isi")})
    {
        QSqlQuery mode(database.db);
        if (!mode.exec("PRAGMA "+schema+".journal_mode") || !mode.next()) return fail(message,mode.lastError().text());
        if (!QStringList{"delete","truncate","persist"}.contains(mode.value(0).toString().toLower()))
            return fail(message,QStringLiteral("原组表日志模式不支持跨文件共同恢复，未改模式或保存部分数据。"));
    }
    return true;
}
}

void OriginalReagentEditor::projectSnapshot(const OriginalReagentState &observed, const Rows &isiRows,
    const ProjectRecords &records, const QMap<QString,QString> &versions,
    ReagentSnapshot *snapshot, ReagentProjects *choices) const
{
    ReagentSnapshot result; result.originalProfile=true; result.originalRoot=root_;
    ReagentProjects projectChoices;
    for (const auto &project : records)
    {
        ReagentProject choice;
        choice.id=project.id; choice.name=project.name; choice.enabled=project.enabled;
        choice.originalRoot=project.originalRoot; choice.originalFileName=project.originalFileName;
        choice.originalFingerprint=project.originalFingerprint;
        choice.originalGroupEnabled=project.originalBaseValues.value(QStringLiteral("is_related"));
        for (const auto &step : project.advanced.reagents)
        {
            const QString name=step.value(QStringLiteral("reagent_name"));
            if (!choice.reagentNames.contains(name)) choice.reagentNames.append(name);
        }
        projectChoices.append(choice);
    }
    for (const auto &row : observed.reagents)
    {
        ReagentRecord r;
        r.id=row.value("id").toLongLong(); r.name=row.value("reagent_name").toString(); r.type=displayedType(row.value("reagent_type").toString());
        r.manufacturer=row.value("manufacturer").toString(); r.warningUl=row.value("warning_ul").toInt(); r.outUl=row.value("out_ul").toInt();
        r.valid=row.value("valid").toBool(); r.checkExpiry=row.value("check_limit").toBool(); r.fixed=row.value("fixed").toBool();
        r.originalValues=row; r.originalFingerprint=versions.value(informationFile); result.reagents.append(r);
    }
    for (const auto &row : observed.bottles)
    {
        ReagentBottle b;
        b.id=row.value("id").toLongLong(); b.name=row.value("bottle_type_name").toString(); b.area=row.value("area").toString();
        b.capacityUl=row.value("max_amount_ul").toInt(); b.heightMm=row.value("bottle_real_height_mm").toDouble();
        b.originalValues=row; b.originalFingerprint=versions.value(bottleFile); result.bottles.append(b);
    }
    for (const auto &row : observed.batchGroups)
    {
        ReagentBatch b; b.id=row.value("id").toLongLong(); b.code=b.batchGroup=row.value("batch_group").toString();
        for (const auto &project : records) if (project.name==row.value("project_name").toString()) b.projectId=project.id;
        for (const auto &metadata : isiRows) if (metadata.value("batch_group").toString()==b.batchGroup)
        { b.hasMetadata=true; b.isiValue=metadata.value("isi_value").toString(); b.normalValue=metadata.value("normal_value").toString(); }
        b.originalValues=row; b.originalFingerprint=versions.value(informationFile); result.batches.append(b);
    }
    for (const auto &row : observed.barcodes)
    {
        ReagentBarcode b; b.id=row.value("id").toLongLong(); b.code=row.value("reagent_barcode").toString();
        b.reagentId=idByName(observed.reagents,"reagent_name",row.value("reagent_name").toString());
        b.bottleId=idByName(observed.bottles,"bottle_type_name",row.value("bottle_type").toString());
        b.expiry=row.value("reagent_limit_date").toString(); b.originalValues=row; b.originalFingerprint=versions.value(informationFile);
        result.barcodes.append(b);
    }
    for (const auto &row : observed.holes)
    {
        ReagentHole h; h.id=row.value("id").toLongLong(); h.name=row.value("hole_name").toString().toUpper(); h.area=row.value("area").toString();
        h.reagentId=idByName(observed.reagents,"reagent_name",row.value("reagent_name").toString());
        h.bottleId=idByName(observed.bottles,"bottle_type_name",row.value("bottle_type").toString());
        h.originalBatch=row.value("reagent_batch").toString(); h.expiry=row.value("out_date").toString();
        h.amountUl=row.value("amount_uL").toDouble(); h.remainingUl=row.value("used_ul").toDouble(); h.priority=row.value("priority").toInt();
        h.changedAt=row.value("datetime").toString(); h.originalValues=row; h.originalFingerprint=versions.value(settingFile); result.holes.append(h);
    }
    loadDisplayTestCounts(root_,observed,records,&result,initialization_);
    *snapshot=result; *choices=projectChoices;
}

bool OriginalReagentEditor::initialize(const QString &root, QString *message, std::shared_ptr<OriginalReagentPageMetadata> metadata)
{
    // 在任何仓库/缓存初始化之前冻结同一次原INI字节及解析值，后续只核它，不续签旧缓存。
    initialization_=OriginalIni::InitializationSource::capture(root,message);
    if (!initialization_ || !initialization_->current(root,message) ||
        !repository_.initialize(root,message,initialization_) || !initialization_->current(root,message)) return false;
    root_=root;
    pageMetadata_ = std::move(metadata);
    repository_.setPageMetadata(pageMetadata_);
    if (!pageMetadata_)
    {
        // N提交后的只读编辑器已由调用者持两锁；无盘面所有者时保留原读取路径，不递归取锁。
        ReagentSnapshot snapshot; ReagentProjects projects;
        return load(&snapshot,&projects,message);
    }
    // 原初始化可能恢复未完复制；恢复入口自行取锁，先保留它的原调用归属。
    ReagentSnapshot snapshot; ReagentProjects projects;
    if (!load(&snapshot,&projects,message)) return false;
    QLockFile projectLock(QDir(root_).filePath(QStringLiteral(".sn100-original-project-write.lock")));
    QLockFile reagentLock(QDir(root_).filePath(QStringLiteral(".sn100-original-reagent-write.lock")));
    if (!projectLock.tryLock(0) || !reagentLock.tryLock(0))
        return fail(message,QStringLiteral("原资料正在保存，未重建试剂盘面批号。"));
    if (!OriginalProjectCopy::noPending(root_,message)) return false;
    const auto ticket = pageMetadata_->sourceTicket();
    // 锁内只采用恢复完成后的当前来源，不拿先前读取代替重建后像。
    if (!load(&snapshot,&projects,message)) return false;
    return (!pageMetadata_ || pageMetadata_->overwrite(root_,state_.holes,{},true,ticket,message)) &&
        initialization_->current(root_,message);
}

bool OriginalReagentEditor::rebuildPageMetadata(const QString &root,
    const OriginalReagentPageMetadata::SourceTicket &expected, QString *message)
{
    if (!initialization_ || !initialization_->current(root_,message)) return false;
    QLockFile projectLock(QDir(root_).filePath(QStringLiteral(".sn100-original-project-write.lock")));
    QLockFile reagentLock(QDir(root_).filePath(QStringLiteral(".sn100-original-reagent-write.lock")));
    if (!projectLock.tryLock(0) || !reagentLock.tryLock(0))
        return fail(message,QStringLiteral("原资料正在保存，盘面批号重建待核。"));
    if (!pageMetadata_) return true;
    if (root!=root_) return fail(message,QStringLiteral("盘面重建请求属于旧资料根，未改变当前批号。"));
    OriginalReagentState saved;
    return repository_.load(&saved,message) && initialization_->current(root_,message) &&
        pageMetadata_->overwrite(root_,saved.holes,{},true,expected,message) && initialization_->current(root_,message);
}

bool OriginalReagentEditor::load(ReagentSnapshot *snapshot, ReagentProjects *choices, QString *message)
{
    if (!initialization_ || !initialization_->current(root_,message)) return false;
    OriginalReagentState observed;
    if (!snapshot || !choices || !repository_.load(&observed,message)) return false;
    OriginalProjectRepository projects;
    ProjectRecords records; ProjectUnits units; QStringList reagents,cleaners,diluents;
    if (!projects.initialize(root_,QString(),message) || !projects.load(&records,&units,&reagents,&cleaners,&diluents,message)) return false;
    Database isi;
    Rows isiRows;
    const QString isiVersion=OriginalProjectRepository::databaseFingerprint(QDir(root_).filePath(isiFile),message);
    if (isiVersion.isEmpty() || !isi.open(root_,isiFile,false,message) || !allRows(isi,"isi_value",&isiRows,message)) return false;
    QMap<QString,QString> versions=observed.versions;
    versions.insert(isiFile,isiVersion);
    for (const auto &project : records)
        versions.insert(QStringLiteral("project/")+project.originalFileName,project.originalFingerprint);
    ReagentSnapshot result;
    ReagentProjects projectChoices;
    projectSnapshot(observed,isiRows,records,versions,&result,&projectChoices);
    for (auto it=versions.cbegin();it!=versions.cend();++it)
        if (it.value()!=OriginalProjectRepository::databaseFingerprint(QDir(root_).filePath(it.key()),message))
            return fail(message,QStringLiteral("读取期间原试剂或项目资料变化，请重新读取。"));
    if (!initialization_->current(root_,message)) return false;
    state_=observed; isiRows_=isiRows; projects_=records; versions_=versions;
    *snapshot=result; *choices=projectChoices;
    return true;
}

bool OriginalReagentEditor::mutate(const QString &file, const QString &expected,
    const std::function<bool(Database &,QString *)> &change, QString *message, const QStringList &metadataOverwrites,
    bool *databaseCommitted, const ProjectRecord *projectSave,
    const QString &groupEnabled, OriginalProjectSaveNotice *result, const std::function<bool()> &claim,
    const std::function<void(const OriginalProjectSaveNotice &)> &committed)
{
    if (!initialization_ || !initialization_->current(root_,message)) return false;
    if (result) *result=OriginalProjectSaveNotice();
    if (databaseCommitted) *databaseCommitted=false;
    QLockFile projectLock(QDir(root_).filePath(QStringLiteral(".sn100-original-project-write.lock")));
    QLockFile reagentLock(QDir(root_).filePath(QStringLiteral(".sn100-original-reagent-write.lock")));
    if (!projectLock.tryLock(0) || !reagentLock.tryLock(0)) return fail(message,QStringLiteral("原项目或试剂正在保存，请稍后重试，草稿保留。"));
    if (file.startsWith(QStringLiteral("project/")) && !OriginalProjectCopy::noPending(root_,message)) return false;
    if (!expected.isEmpty() && expected!=versions_.value(file)) return fail(message,QStringLiteral("试剂草稿已过期，请重新读取后编辑。"));
    for (auto it=versions_.cbegin();it!=versions_.cend();++it)
        if (it.value()!=OriginalProjectRepository::databaseFingerprint(QDir(root_).filePath(it.key()),message))
            return fail(message,QStringLiteral("原试剂或项目已被其他操作修改，本次没有覆盖，请重新读取。"));
    const auto metadataTicket = pageMetadata_ ? pageMetadata_->sourceTicket() : OriginalReagentPageMetadata::SourceTicket();
    const bool reportCommit=projectSave && (result || committed);
    OriginalProjectSaveNotice notice;
    if (reportCommit)
    {
        if (file!=QStringLiteral("project/")+projectSave->originalFileName || expected!=projectSave->originalFingerprint)
            return fail(message,QStringLiteral("组开关实际写入目标与预检文件不同，尚未提交。"));
        notice.root=root_; notice.originalFileName=projectSave->originalFileName;
        notice.projectId=projectSave->id; notice.projectName=projectSave->name; notice.beforeFingerprint=expected;
        if (claim && !claim()) return fail(message,QStringLiteral("组开关保存预留已取消或过期，尚未提交，草稿保留。"));
    }
    {
        Database database;
        if (!database.open(root_,file,true,message) || !database.begin(message)) return false;
        if (!change(database,message) || !initialization_->current(root_,message)) { database.rollback(); return false; }
        if (!database.commit(message)) return false;
        notice.commitSeen=true;
        if (databaseCommitted) *databaseCommitted=true;
    }
    if (pageMetadata_ && !metadataOverwrites.isEmpty())
    {
        OriginalReagentState saved;
        if (!repository_.load(&saved,message) ||
            !pageMetadata_->overwrite(root_,saved.holes,metadataOverwrites,false,metadataTicket,message))
        {
            pageMetadata_->invalidate(metadataTicket);
            if (message) *message = QStringLiteral("本地资料已提交，但盘面批号来源未重建：") + *message;
            return false;
        }
    }
    if (!reportCommit) return true; // 其他试剂资料不产生项目同步意图。
    OriginalProjectRepository probe;
    ProjectRecords projects; ProjectUnits units; QStringList reagents,cleaners,diluents;
    bool loaded=probe.initialize(root_,QString(),message) && probe.load(&projects,&units,&reagents,&cleaners,&diluents,message);
    int matches=0;
    if (loaded)
        for (const auto &actual : projects)
            if (actual.originalFileName==notice.originalFileName && actual.id==notice.projectId && actual.name==notice.projectName &&
                actual.originalBaseValues.value(QStringLiteral("is_related"))==groupEnabled && !actual.originalFingerprint.isEmpty() &&
                actual.originalFingerprint==OriginalProjectRepository::databaseFingerprint(QDir(root_).filePath(file),message))
            { ++matches; notice.committedFingerprint=actual.originalFingerprint; }
    notice.verifiedSnapshot=matches==1;
    if (!notice.verifiedSnapshot)
    {
        notice.committedFingerprint.clear();
        if (message->isEmpty()) *message=QStringLiteral("组开关已提交文件的身份、开关值或版本未核实。");
    }
    notice.message=*message;
    if (result) *result=notice;
    if (committed) committed(notice); // 写连接已关闭，两个原文件锁仍持有；只排值对象。
    return notice.verifiedSnapshot;
}

bool OriginalReagentEditor::saveRow(Database &database, const QString &table, qint64 id, QVariantMap values, QString *message)
{
    values.remove("id");
    // 只更新本页明确负责的列，不删表重建，也不覆盖未知厂商列。
    QStringList columns,places,assignments; QVariantList arguments;
    for (auto it=values.cbegin();it!=values.cend();++it)
    {
        if (!QRegularExpression("^[A-Za-z_][A-Za-z0-9_]*$").match(it.key()).hasMatch()) return fail(message,QStringLiteral("试剂列名非法。"));
        const QString key='"'+it.key()+'"'; columns.append(key); places.append("?"); assignments.append(key+"=?"); arguments.append(it.value());
    }
    if (id)
    {
        arguments.append(id);
        return database.execute(QStringLiteral("UPDATE \"%1\" SET %2 WHERE id=?").arg(table,assignments.join(',')),arguments,message,1);
    }
    return database.execute(QStringLiteral("INSERT INTO \"%1\"(%2) VALUES(%3)").arg(table,columns.join(','),places.join(',')),arguments,message,1);
}

bool OriginalReagentEditor::projectUses(const QString &name, bool, QString *message) const
{
    for (const auto &project : projects_)
    {
        if (project.originalBaseValues.value("IBS_name")==name) return true;
        Database database;
        if (!database.open(root_,QStringLiteral("project/")+project.originalFileName,false,message)) return true;
        for (const auto &table : {QStringLiteral("project_sample_rate"),QStringLiteral("project_reagent")})
        {
            QSqlQuery query(database.db);
            if (!query.exec(QStringLiteral("SELECT * FROM ")+table)) { fail(message,query.lastError().text()); return true; }
            const auto columns=query.record();
            while (query.next())
                for (int i=0;i<columns.count();++i)
                    if (columns.fieldName(i).endsWith("_name") && query.value(i).toString()==name) return true;
        }
    }
    return false;
}

bool OriginalReagentEditor::saveReagent(const ReagentRecord &r, QString *message)
{
    const int type=shownTypes.indexOf(r.type);
    if (!nameValid(r.name,16) || type<0 || r.outUl<0 || r.warningUl<r.outUl || r.warningUl>9999)
        return fail(message,QStringLiteral("试剂名称、类型或警告量/缺少量不合法。"));
    const Row *before=byId(state_.reagents,r.id);
    const QString oldName=before ? before->value("reagent_name").toString() : QString();
    const bool changed=before && (oldName!=r.name || before->value("reagent_type").toString()!=storedTypes[type] || !r.valid);
    if (changed)
    {
        bool referenced=projectUses(oldName,false,message);
        for (const auto &row : state_.holes) if (row.value("reagent_name").toString()==oldName) referenced=true;
        for (const auto &row : state_.barcodes) if (row.value("reagent_name").toString()==oldName) referenced=true;
        if (referenced) return fail(message,QStringLiteral("原项目、孔位或条码仍引用该试剂，不能改名、改类型或停用；先解除引用。"));
    }
    for (const auto &row : state_.reagents)
        if (row.value("id").toLongLong()!=r.id && row.value("reagent_name").toString()==r.name) return fail(message,QStringLiteral("原库已有同名试剂。"));
    QVariantMap values{{"reagent_name",r.name},{"reagent_type",storedTypes[type]},{"manufacturer",r.manufacturer},
                       {"valid",r.valid ? "1":"0"},{"check_limit",r.checkExpiry ? "1":"0"}};
    changedInteger(&values,before,"warning_ul",r.warningUl); changedInteger(&values,before,"out_ul",r.outUl);
    for (const auto &key : {"banding_needle","dect_clean","cleaner_name"})
        if (r.originalValues.contains(key)) values.insert(key,r.originalValues.value(key));
    if (!r.id)
        for (const auto &key : {"reagent_code","ref_use_ul","useage_ul","fixed"}) values.insert(key,r.originalValues.value(key,QStringLiteral("0")));
    return mutate(informationFile,r.originalFingerprint,[&](Database &db,QString *m){ return saveRow(db,"reagent_list",r.id,values,m); },message);
}

bool OriginalReagentEditor::saveBottle(const ReagentBottle &b, QString *message)
{
    if (!nameValid(b.name,32) || (b.area!="r_area" && b.area!="s_area") || b.capacityUl<1 || b.capacityUl>100000 || !std::isfinite(b.heightMm) || b.heightMm<0 || b.heightMm>500)
        return fail(message,QStringLiteral("瓶型名称、区域、容量或高度不合法。"));
    const Row *before=byId(state_.bottles,b.id);
    if (before)
    {
        const QString name=before->value("bottle_type_name").toString();
        for (const auto &row : state_.holes) if (row.value("bottle_type").toString()==name) return fail(message,QStringLiteral("瓶型已被原孔位引用，请先解除配置。"));
        for (const auto &row : state_.barcodes) if (row.value("bottle_type").toString()==name) return fail(message,QStringLiteral("瓶型已被原条码引用，请先解除配置。"));
    }
    for (const auto &row : state_.bottles)
        if (row.value("id").toLongLong()!=b.id && row.value("bottle_type_name").toString()==b.name && row.value("area").toString()==b.area)
            return fail(message,QStringLiteral("该区域已有同名瓶型。"));
    QVariantMap values{{"bottle_type_name",b.name},{"area",b.area},{"max_amount_ul",b.capacityUl},{"bottle_real_height_mm",b.heightMm}};
    if (!b.id) for (const auto &key : {"is_adpter","bottom_diameter","angle"}) values.insert(key,b.originalValues.value(key,QStringLiteral("0")));
    return mutate(bottleFile,b.originalFingerprint,[&](Database &db,QString *m){ return saveRow(db,"reagent_bottle_type",b.id,values,m); },message);
}

bool OriginalReagentEditor::saveBarcode(const ReagentBarcode &b, QString *message)
{
    const Row *reagent=byId(state_.reagents,b.reagentId), *bottle=byId(state_.bottles,b.bottleId);
    QDate expiry=QDate::fromString(b.expiry,Qt::ISODate);
    if (!expiry.isValid()) expiry=QDate::fromString(b.expiry,"yyyy/MM/dd");
    if (!reagent || !bottle || !nameValid(b.code,128) || !expiry.isValid()) return fail(message,QStringLiteral("原条码、试剂、瓶型或有效期不合法。"));
    for (const auto &row : state_.barcodes) if (row.value("id").toLongLong()!=b.id && row.value("reagent_barcode").toString()==b.code)
        return fail(message,QStringLiteral("原库已有相同条码。"));
    QVariantMap values{{"reagent_name",reagent->value("reagent_name")},{"reagent_barcode",b.code},{"reagent_limit_date",b.expiry},{"bottle_type",bottle->value("bottle_type_name")}};
    if (!b.id) values.insert("isi_value",b.originalValues.value("isi_value",QString()));
    return mutate(informationFile,b.originalFingerprint,[&](Database &db,QString *m){ return saveRow(db,"reagent_barcode",b.id,values,m); },message);
}

bool OriginalReagentEditor::saveHole(const ReagentHole &h, QString *message)
{
    const Row *before=byId(state_.holes,h.id), *reagent=byId(state_.reagents,h.reagentId), *bottle=byId(state_.bottles,h.bottleId);
    if (!before || h.name!=before->value("hole_name").toString().toUpper() || !reagent || !bottle || !reagent->value("valid").toBool() ||
        h.area!=before->value("area").toString() || h.area!=bottle->value("area").toString() || !std::isfinite(h.amountUl) || !std::isfinite(h.remainingUl) ||
        h.amountUl<0 || h.remainingUl<0 || h.remainingUl>h.amountUl || h.amountUl>bottle->value("max_amount_ul").toDouble() || h.priority<0 || h.priority>999)
        return fail(message,QStringLiteral("原孔位身份、试剂、瓶型区域或液量不合法，未改写。"));
    const QString type=reagent->value("reagent_type").toString();
    if (type!="cleaner" && (h.area=="r_area" ? type!="r_reagent" : type!="s_ibs")) return fail(message,QStringLiteral("该试剂类型不能配置到所选区域。"));
    QDate expiry=QDate::fromString(h.expiry,Qt::ISODate); if (!expiry.isValid()) expiry=QDate::fromString(h.expiry,"yyyy/MM/dd");
    if (!expiry.isValid()) return fail(message,QStringLiteral("原孔位有效期格式无效。"));
    const QString batch=h.originalBatch.isEmpty() ? QStringLiteral("Default") : h.originalBatch;
    if (!nameValid(batch,128)) return fail(message,QStringLiteral("试剂批号格式无效。"));
    QVariantMap values{{"reagent_type",type},{"reagent_name",reagent->value("reagent_name")},{"reagent_batch",batch},
        {"bottle_type",bottle->value("bottle_type_name")},{"amount_uL",h.amountUl},{"used_ul",h.remainingUl},{"priority",h.priority},
        {"out_date",h.expiry},{"datetime",QDateTime::currentDateTime().toString("yyyy/MM/dd HH:mm:ss")},{"state","r_normal"},
        {"out_ul",reagent->value("out_ul")},{"warning_ul",reagent->value("warning_ul")},{"max_ul",bottle->value("max_amount_ul")}};
    return mutate(settingFile,h.originalFingerprint,[&](Database &db,QString *m){ return saveRow(db,state_.settingTable,h.id,values,m); },message,{before->value("hole_name").toString()});
}

bool OriginalReagentEditor::clearHole(const QString &name, QString *message)
{
    qint64 id=0; QString area, originalName;
    for (const auto &row : state_.holes) if (row.value("hole_name").toString().compare(name,Qt::CaseInsensitive)==0)
    { id=row.value("id").toLongLong(); area=row.value("area").toString(); originalName=row.value("hole_name").toString(); }
    if (!id) return fail(message,QStringLiteral("原孔位不存在，未创建或删除孔位行。"));
    // 原cusReagentHole::clearHoleSet按区域保留类型，清空批号/瓶型及本孔容量阈值。
    QVariantMap values{{"reagent_type",area=="r_area" ? "r_reagent" : "s_ibs"},{"reagent_name","none"},{"reagent_batch",""},{"bottle_type",""},
        {"amount_uL","0"},{"used_ul","0"},{"state","r_none"},{"out_date",""},{"datetime",""},
        {"priority","0"},{"warning_ul","0"},{"out_ul","0"},{"max_ul","0"}};
    return mutate(settingFile,QString(),[&](Database &db,QString *m){ return saveRow(db,state_.settingTable,id,values,m); },message,{originalName});
}

bool OriginalReagentEditor::prepareSingleProbe(const OriginalReagentProbe::Request &request,
    OriginalReagentProbe::Prepared *prepared, QString *message)
{
    using namespace OriginalReagentProbe;
    if (!prepared) return fail(message,QStringLiteral("单孔探测准备输出为空，未发送。"));
    *prepared=Prepared(); prepared->source=request;
    const auto current=[&](QString *error)
    {
        if (Clock::now()>=request.prepareUntil)
            return fail(error,QStringLiteral("单孔探测准备期限已过，未继续准备或发送。"));
        if (!initialization_ || !initialization_->current(root_,error)) return false;
        if (!probeProjectFilesMatch(root_,projects_,versions_,error)) return false;
        for (auto it=versions_.cbegin();it!=versions_.cend();++it)
            if (it.value().isEmpty() || it.value()!=OriginalProjectRepository::databaseFingerprint(QDir(root_).filePath(it.key()),error))
                return fail(error,QStringLiteral("原试剂或项目缓存已变化，未给旧资料续签探测来源。"));
        return Clock::now()<request.prepareUntil || fail(error,QStringLiteral("单孔探测准备期限已过，未返回旧来源。"));
    };
    if (request.token.isEmpty() || !request.request || !request.connection || request.root!=root_ ||
        state_.root!=root_ || versions_.isEmpty() || request.prepareUntil>Clock::now()+std::chrono::seconds(10) ||
        !request.actor.authenticated ||
        request.actor.role<0 || request.actor.role>5 || !current(message))
        return fail(message,message && !message->isEmpty() ? *message : QStringLiteral("单孔探测请求、账户或资料根未就绪。"));
    const Row *selected=nullptr;
    for (int index : operationPanelHoles(state_))
        if (state_.holes[index].value(QStringLiteral("hole_name")).toString()==request.hole)
        {
            if (selected) return fail(message,QStringLiteral("当前面板孔名重复，未猜测单孔目标。"));
            selected=&state_.holes[index];
        }
    if (!selected) return fail(message,QStringLiteral("单孔探测目标不属于当前原面板。"));
    bool validId=false;
    const qint64 id=selected->value(QStringLiteral("id")).toLongLong(&validId);
    const QStringList configuredStates{QStringLiteral("r_normal"),QStringLiteral("r_warning"),
        QStringLiteral("r_useout"),QStringLiteral("r_outdate")};
    if (!validId || id<=0 || !configuredStates.contains(selected->value(QStringLiteral("state")).toString()))
        return fail(message,QStringLiteral("本片只准备合法已配置孔，不准备None、Unknown或未知状态。"));
    if (!pageMetadata_) return fail(message,QStringLiteral("真实试剂盘面批号来源未绑定，未准备单孔探测。"));
    // 批号只捕获/核当前，不发布CAL setter或元数据覆盖。
    OriginalReagentPageMetadata::View batches;
    if (!pageMetadata_->capture(root_,state_.holes,&batches,message)) return false;
    ProjectRecords projects;
    if (!orderedOriginalProjects(root_,projects_,&projects,initialization_,message)) return false;
    QMap<QString,int> needles;
    const QString reagent=selected->value(QStringLiteral("reagent_name")).toString();
    if (!singleProbeNeedles(root_,projects,reagent,request.prepareUntil,&needles,message)) return false;
    QByteArray payload;
    if (!repository_.detectionPayload(state_,QStringList{request.hole},needles,&payload,message) ||
        !payload.startsWith(QByteArrayLiteral("Mg[1]")) || !current(message) || !pageMetadata_->current(batches,message))
        return fail(message,message && !message->isEmpty() ? *message : QStringLiteral("没有得到唯一单孔Mg，未发送。"));
    Prepared result;
    result.source=request; result.table=state_.settingTable; result.payload=payload;
    result.originalHole=*selected; result.versions=versions_;
    for (auto it=initialization_->versions().cbegin();it!=initialization_->versions().cend();++it)
        result.versions.insert(it.key(),it.value());
    result.initialization=initialization_; result.batches=batches;
    result.success=true;
    result.message=QStringLiteral("已准备唯一合法配置孔Mg；没有写库、发送或确认设备动作。");
    *prepared=result;
    return true;
}

bool OriginalReagentEditor::prepareDeviceOperation(const QString &operation, const QString &holeName,
    bool linuxNew, QList<QByteArray> *packets, bool *localSaved, QString *message,
    const std::function<bool()> &stillCurrent)
{
    if (!packets || !localSaved) return fail(message,QStringLiteral("试剂操作输出为空，未准备或保存资料。"));
    packets->clear(); *localSaved=false;
    if (root_.isEmpty() || state_.root!=root_ || versions_.value(settingFile).isEmpty())
        return fail(message,QStringLiteral("原试剂资料尚未读取，未准备设备操作。"));
    const auto unchanged=[&](QString *error)
    {
        if (!initialization_ || !initialization_->current(root_,error)) return false;
        if (!stillCurrent || !stillCurrent())
            return fail(error,QStringLiteral("试剂操作的连接或会话许可已经改变，未继续准备或保存。"));
        for (auto it=versions_.cbegin();it!=versions_.cend();++it)
            if (it.value()!=OriginalProjectRepository::databaseFingerprint(QDir(root_).filePath(it.key()),error))
                return fail(error,QStringLiteral("试剂操作前原项目或试剂资料已变化，请重新读取；未保存或发送。"));
        return true;
    };
    if (!unchanged(message)) return false;

    OriginalReagentState proposed=state_;
    OriginalReagentPageMetadata::View metadataView;
    if (pageMetadata_ && !pageMetadata_->capture(state_.root,state_.holes,&metadataView,message)) return false;
    const QList<int> panel=operationPanelHoles(proposed);
    QSet<qint64> ids;
    for (int index : panel)
    {
        bool ok=false;
        const qint64 id=proposed.holes[index].value("id").toString().toLongLong(&ok);
        if (!ok || id<=0 || ids.contains(id))
            return fail(message,QStringLiteral("原试剂孔位编号无效或重复，未新增、合并或改写孔位。"));
        ids.insert(id);
    }
    int selected=-1;
    for (int index : panel)
        if (proposed.holes[index].value("hole_name").toString()==holeName)
        {
            if (selected>=0) return fail(message,QStringLiteral("当前面板存在重复孔名，未选择其中一个。"));
            selected=index;
        }
    const bool detection=operation==QStringLiteral("detectOne") || operation==QStringLiteral("detectAll") ||
                         operation==QStringLiteral("fillHole");
    if (operation!=QStringLiteral("clearPanel") && operation!=QStringLiteral("detectAll") && selected<0)
        return fail(message,QStringLiteral("选中孔位不在当前原试剂面板中，未创建或猜测孔位。"));
    HoleChanges changes;
    QList<QByteArray> prepared;
    const auto configUnchanged=[&](QString *error)
    {
        if (!stillCurrent || !stillCurrent())
            return fail(error,QStringLiteral("试剂操作许可已经改变，当前事务不继续提交。"));
        return initialization_ && initialization_->current(root_,error);
    };

    if (operation==QStringLiteral("prioritizeHole"))
    {
        if (linuxNew)
        {
            QByteArray payload;
            auto sending = proposed;
            if (metadataView.available)
                for (auto &row : sending.holes)
                    row[QStringLiteral("reagent_batch")] = metadataView.batches.value(row.value(QStringLiteral("id")).toLongLong());
            if (!repository_.singleReagentPriorityPayload(sending,holeName,&payload,message)) return false;
            prepared.append(payload);
        }
        else if (proposed.holes[selected].value("state").toString()!=QStringLiteral("r_none"))
        {
            const QString name=proposed.holes[selected].value("reagent_name").toString();
            if (metadataView.available)
                for (int index : panel)
                    if (index==selected || (proposed.holes[index].value("state").toString()!=QStringLiteral("r_none") &&
                        proposed.holes[index].value("reagent_name").toString()==name))
                        changeOperationHole(&proposed,index,QStringLiteral("reagent_batch"),
                            metadataView.batches.value(proposed.holes[index].value("id").toLongLong()),&changes);
            changeOperationHole(&proposed,selected,QStringLiteral("priority"),1,&changes);
            for (int index : panel)
            {
                const auto &hole=proposed.holes[index];
                if (index==selected || hole.value("state").toString()==QStringLiteral("r_none") ||
                    hole.value("reagent_name").toString()!=name) continue;
                bool ok=false;
                const int priority=hole.value("priority").toString().toInt(&ok);
                if (!ok || priority<0 || priority==std::numeric_limits<int>::max())
                    return fail(message,QStringLiteral("同名试剂优先级无效或不能继续递增，未保存。"));
                changeOperationHole(&proposed,index,QStringLiteral("priority"),priority+1,&changes);
            }
            if (!arrangeOperationPriority(&proposed,panel,QStringLiteral("r_area"),name,&changes,message) ||
                !arrangeOperationPriority(&proposed,panel,QStringLiteral("s_area"),name,&changes,message)) return false;
        }
    }
    else if (operation==QStringLiteral("fillHole"))
    {
        const auto before=proposed.holes[selected];
        if (metadataView.available)
            changeOperationHole(&proposed,selected,QStringLiteral("reagent_batch"),
                metadataView.batches.value(before.value("id").toLongLong()),&changes);
        const Row *bottle=nullptr;
        for (const auto &row : proposed.bottles)
            if (row.value("bottle_type_name").toString()==before.value("bottle_type").toString() &&
                row.value("area").toString()==before.value("area").toString()) { bottle=&row; break; }
        double maximum=0;
        if (!bottle || !finiteNumber(bottle->value("max_amount_ul"),&maximum) || maximum<0)
            return fail(message,QStringLiteral("当前瓶型及区域没有有效最大装载量，未充满或发送探测。"));
        // 原fullHole只表示使用者确认已充满，不能把它标记为设备实测液面。
        for (const auto &field : {QStringLiteral("max_ul"),QStringLiteral("amount_uL"),QStringLiteral("used_ul")})
            changeOperationHole(&proposed,selected,field,QString::number(maximum),&changes);
        changeOperationHole(&proposed,selected,QStringLiteral("state"),QStringLiteral("r_normal"),&changes);
        changeOperationHole(&proposed,selected,QStringLiteral("datetime"),QDateTime::currentDateTime().toString("yyyy/MM/dd HH:mm:ss"),&changes);
        changeOperationHole(&proposed,selected,QStringLiteral("priority"),99,&changes);
        if (!linuxNew)
        {
            // 原slotHoleInfoChanged在旧协议下重排盘上每种试剂，而不仅是本次选中试剂。
            QStringList names;
            for (int index : panel)
                if (proposed.holes[index].value("state").toString()!=QStringLiteral("r_none"))
                {
                    const QString name=proposed.holes[index].value("reagent_name").toString();
                    if (!names.contains(name)) names.append(name);
                }
            for (const auto &name : names)
                if (!arrangeOperationPriority(&proposed,panel,QStringLiteral("r_area"),name,&changes,message) ||
                    !arrangeOperationPriority(&proposed,panel,QStringLiteral("s_area"),name,&changes,message)) return false;
        }
        if (linuxNew)
        {
            QByteArray payload;
            auto sending = proposed;
            if (metadataView.available)
                for (auto &row : sending.holes)
                    row[QStringLiteral("reagent_batch")] = metadataView.batches.value(row.value(QStringLiteral("id")).toLongLong());
            if (!repository_.singleReagentUpdatePayload(sending,holeName,true,&payload,message)) return false;
            prepared.append(payload);
        }
    }
    else if (operation==QStringLiteral("clearPanel"))
    {
        if (panel.isEmpty()) return fail(message,QStringLiteral("当前原试剂面板没有孔位，未清盘。"));
        for (int index : panel)
        {
            const QString area=proposed.holes[index].value("area").toString();
            if (area!=QStringLiteral("r_area") && area!=QStringLiteral("s_area"))
                return fail(message,QStringLiteral("原孔位区域未识别，未清空部分盘面。"));
            const QVariantMap cleared{{"reagent_type",area==QStringLiteral("r_area") ? "r_reagent" : "s_ibs"},
                {"reagent_name","none"},{"reagent_batch",""},{"bottle_type",""},{"amount_uL","0"},
                {"priority",0},{"state","r_none"},{"datetime",""},{"out_date",""},{"warning_ul","0"},
                {"out_ul","0"},{"max_ul","0"}};
            // 原clearHoleSet不清usedVolumn；随后updateHoleInfo仍将旧used_ul落库。
            for (auto it=cleared.cbegin();it!=cleared.cend();++it)
                changeOperationHole(&proposed,index,it.key(),it.value(),&changes);
        }
        prepared.append(QByteArrayLiteral("UC"));
    }
    else if (!detection)
        return fail(message,QStringLiteral("该试剂操作尚无已接入的原处理，不生成替代动作。"));

    if (detection)
    {
        ProjectRecords projects;
        if (!orderedOriginalProjects(root_,projects_,&projects,initialization_,message)) return false;
        QStringList holes;
        if (operation==QStringLiteral("detectAll"))
        {
            static const QStringList states{"r_none","r_normal","r_warning","r_useout","r_unknown","r_outdate"};
            for (int index : panel)
            {
                const auto &hole=proposed.holes[index];
                const QString state=hole.value("state").toString();
                if (!states.contains(state)) return fail(message,QStringLiteral("原孔位状态未识别，未猜测是否参与整盘探测。"));
                if (state!=QStringLiteral("r_none") && state!=QStringLiteral("r_unknown"))
                    holes.append(hole.value("hole_name").toString());
            }
        }
        else holes.append(proposed.holes[selected].value("hole_name").toString());
        if (holes.isEmpty()) return fail(message,QStringLiteral("请选择需要探测的孔位。"));
        QMap<QString,int> needleSelection;
        for (const auto &project : projects)
            for (const auto &step : project.advanced.reagents)
            {
                const QString reagentName=step.value(QStringLiteral("reagent_name"));
                for (int index : panel)
                    if (holes.contains(proposed.holes[index].value("hole_name").toString()) &&
                        proposed.holes[index].value("reagent_name").toString()==reagentName)
                    {
                        // 原dectLV按项目顺序覆盖同名映射；原文本“左试剂针”才选1，否则选2。
                        needleSelection[reagentName]=step.value(QStringLiteral("needle_select"))==QStringLiteral("左试剂针") ? 1 : 2;
                        break;
                    }
            }
        QByteArray payload;
        if (!repository_.detectionPayload(proposed,holes,needleSelection,&payload,message)) return false;
        prepared.append(payload);
    }

    if (!unchanged(message) || !configUnchanged(message)) return false;
    // 原充满/旧优先级的实际落库孔继续保存当前控件批号；不为未变孔新增更新。
    QStringList overwritten;
    if (operation==QStringLiteral("clearPanel"))
        for (int index : panel) overwritten.append(proposed.holes[index].value(QStringLiteral("hole_name")).toString());
    for (auto it=changes.begin();it!=changes.end();++it)
        for (const auto &row : state_.holes)
            if (row.value(QStringLiteral("id")).toLongLong()==it.key())
            {
                const QString name=row.value(QStringLiteral("hole_name")).toString();
                if (!overwritten.contains(name)) overwritten.append(name);
                if (metadataView.available && operation!=QStringLiteral("clearPanel"))
                    it.value().insert(QStringLiteral("reagent_batch"),metadataView.batches.value(it.key()));
            }
    if (!changes.isEmpty() || operation==QStringLiteral("clearPanel"))
    {
        // 原updateHoleInfo未传formula_level；旧TableDal全列更新把它写为空串。
        // 只在原来确实会落库的孔复现该结果，未知厂商列仍保留。
        for (auto it=changes.begin();it!=changes.end();++it) it.value().insert(QStringLiteral("formula_level"),QString());
        bool committed=false;
        const bool saved=mutate(settingFile,versions_.value(settingFile),[&](Database &database,QString *error)
        {
            if (!configUnchanged(error) ||
                (pageMetadata_ && !pageMetadata_->current(metadataView,error))) return false;
            for (auto it=changes.cbegin();it!=changes.cend();++it)
                if (!saveRow(database,state_.settingTable,it.key(),it.value(),error)) return false;
            return configUnchanged(error);
        },message,overwritten,&committed);
        *localSaved=committed; // 提交后来源核对失败也保留事实；失败时仍不发送命令。
        if (!saved) return false;
    }
    if (pageMetadata_ && changes.isEmpty() && operation!=QStringLiteral("clearPanel") &&
        !pageMetadata_->current(metadataView,message)) return false;
    *packets=prepared;
    if (message)
        *message=*localSaved ? QStringLiteral("原本地资料已保存；设备命令仅完成准备，执行及回读仍待确认。") :
            prepared.isEmpty() ? QStringLiteral("原版对当前未设置孔位没有执行优先级变更，未修改资料或发送命令。") :
                                QStringLiteral("原试剂命令已准备，尚未发送或确认设备执行。" );
    return true;
}

bool OriginalReagentEditor::saveBatchGroupEnabled(const ReagentProject &requested, bool enabled, QString *message,
    OriginalProjectSaveNotice *result, const std::function<bool()> &claim,
    const std::function<void(const OriginalProjectSaveNotice &)> &committed)
{
    if (result) *result=OriginalProjectSaveNotice();
    const ProjectRecord *project=nullptr;
    for (const auto &candidate : projects_)
        if (candidate.id==requested.id)
        {
            if (project) return fail(message,QStringLiteral("原项目编号重复，未选择其中一个文件。"));
            project=&candidate;
        }
    if (!project || requested.originalRoot!=root_ || project->originalRoot!=root_ ||
        requested.name!=project->name || requested.originalFileName!=project->originalFileName ||
        requested.originalFingerprint.isEmpty() || requested.originalFingerprint!=project->originalFingerprint ||
        requested.originalGroupEnabled!=project->originalBaseValues.value(QStringLiteral("is_related")))
        return fail(message,QStringLiteral("原项目或试剂组开关草稿已过期，请重新读取后操作。"));
    if (requested.originalGroupEnabled!=QStringLiteral("0") && requested.originalGroupEnabled!=QStringLiteral("1"))
        return fail(message,QStringLiteral("原项目is_related缺失或不是0/1，未猜测或补建该字段。"));
    const QString file=QStringLiteral("project/")+project->originalFileName;
    const QString value=enabled ? QStringLiteral("1") : QStringLiteral("0");
    return mutate(file,requested.originalFingerprint,[&](Database &database,QString *error)
    {
        // BEGIN IMMEDIATE已取得SQLite写锁；补核整个目标版本，关闭旧指纹到写锁之间的间隙。
        if (OriginalProjectRepository::databaseFingerprint(QDir(root_).filePath(file),error)!=requested.originalFingerprint)
            return fail(error,QStringLiteral("取得写事务前原项目已变化，本次开关未写入，请重新读取。"));
        QSqlQuery original(database.db);
        if (!original.prepare(QStringLiteral("SELECT value FROM project_base WHERE name=?")))
            return fail(error,original.lastError().text());
        original.addBindValue(QStringLiteral("is_related"));
        if (!original.exec()) return fail(error,original.lastError().text());
        if (!original.next() || original.value(0).isNull() || original.value(0).toString()!=requested.originalGroupEnabled)
            return fail(error,QStringLiteral("原项目试剂组开关已变化或缺失，请重新读取。"));
        if (original.next()) return fail(error,QStringLiteral("原项目is_related重复，未更新其中一行。"));
        original.finish();
        if (enabled)
        {
            // 原getData不排序；必须按它实际读取的顺序排除最后一行，不能套用results的result_id排序。
            QSqlQuery results(database.db);
            if (!results.exec(QStringLiteral("SELECT * FROM project_results")))
                return fail(error,results.lastError().text());
            const int usedColumn=results.record().indexOf(QStringLiteral("is_used"));
            if (usedColumn<0) return fail(error,QStringLiteral("原项目结果表缺少is_used，无法核对启用条件。"));
            QList<int> used;
            while (results.next())
            {
                const QString raw=results.value(usedColumn).toString();
                if (results.value(usedColumn).isNull() || (raw!=QStringLiteral("0") && raw!=QStringLiteral("1")))
                    return fail(error,QStringLiteral("原项目结果启用值不是0/1，未猜测校准条件。"));
                used.append(raw.toInt());
            }
            if (results.lastError().isValid()) return fail(error,results.lastError().text());
            results.finish();
            int calibrationUse=0;
            for (int index=0;index+1<used.size();++index) calibrationUse+=used.at(index);
            if (calibrationUse<=1) return fail(error,QStringLiteral("该项目无校准任务，启用失败！"));
            int groups=0;
            for (const auto &group : state_.batchGroups)
                if (group.value(QStringLiteral("project_name")).toString()==project->name &&
                    group.value(QStringLiteral("batch_group")).toString()!=QStringLiteral("Default") &&
                    group.value(QStringLiteral("batch_group")).toString()!=QStringLiteral("PTFbg")) ++groups;
            if (!groups) return fail(error,QStringLiteral("试剂组为空，无法开启组分管理，请先添加试剂组！"));
        }
        if (!database.execute(QStringLiteral("UPDATE project_base SET value=? WHERE name=? AND value=?"),
                              {value,QStringLiteral("is_related"),requested.originalGroupEnabled},error,1)) return false;
        // 项目自身在写事务内；关联试剂资料在提交前再核版本，不能以旧批组放行。
        for (auto it=versions_.cbegin();it!=versions_.cend();++it)
            if (it.key()!=file && it.value()!=OriginalProjectRepository::databaseFingerprint(QDir(root_).filePath(it.key()),error))
                return fail(error,QStringLiteral("保存期间关联试剂或项目资料已变化，本次开关没有提交，请重新读取。"));
        return true;
    },message,{},nullptr,project,value,result,claim,committed);
}

bool OriginalReagentEditor::saveBatch(const ReagentBatch &b, QString *message)
{
    QString projectName;
    for (const auto &project : projects_) if (project.id==b.projectId && project.enabled) projectName=project.name;
    const Row *before=byId(state_.batchGroups,b.id);
    if (projectName.isEmpty() || b.batchGroup.isEmpty() || b.batchGroup.size()>256)
        return fail(message,QStringLiteral("请先选择有效原项目并设置试剂批号组。"));
    QSet<QString> seen;
    for (const auto &member : b.batchGroup.split('|',Qt::KeepEmptyParts))
    {
        if (!nameValid(member,32) || seen.contains(member)) return fail(message,QStringLiteral("试剂组含空批号、重复批号或非法文字。"));
        seen.insert(member);
    }
    const QString oldGroup=before ? before->value("batch_group").toString() : QString();
    if (before && (oldGroup=="Default" || oldGroup=="None" || oldGroup=="PTFbg") && oldGroup!=b.batchGroup)
        return fail(message,QStringLiteral("原内置试剂组不能改名。"));
    for (const auto &row : state_.batchGroups)
        if (row.value("id").toLongLong()!=b.id && row.value("project_name").toString()==projectName && row.value("batch_group").toString()==b.batchGroup)
            return fail(message,QStringLiteral("原项目已有相同试剂组。"));
    const QRegularExpression decimal("^(0|[1-9][0-9]{0,3})(\\.[0-9]{1,4})?$");
    if (b.metadataEdited && (!decimal.match(b.isiValue).hasMatch() || !decimal.match(b.normalValue).hasMatch() ||
                             b.isiValue.toDouble()>9999 || b.normalValue.toDouble()>9999))
        return fail(message,QStringLiteral("ISI及正常测定值须同时填写，范围0至9999、最多4位小数。"));
    return mutate(informationFile,b.originalFingerprint,[&](Database &db,QString *m)
    {
        // 原组表和isi_Value.db分开；同一SQLite连接提交，避免只改一半的组名/参数。
        if (!attachIsi(db,root_,m)) return false;
        if (!saveRow(db,"reagent_batch_group",b.id,{{"project_name",projectName},{"batch_group",b.batchGroup}},m)) return false;
        const Row *metadata=nullptr;
        for (const auto &row : isiRows_) if (row.value("batch_group").toString()==(oldGroup.isEmpty() ? b.batchGroup : oldGroup))
        { if (metadata) return fail(m,QStringLiteral("原ISI表存在同名重复记录，未选择其中一条。")); metadata=&row; }
        if (metadata)
        {
            const QString isi=b.metadataEdited ? b.isiValue : metadata->value("isi_value").toString();
            const QString normal=b.metadataEdited ? b.normalValue : metadata->value("normal_value").toString();
            return db.execute("UPDATE isi.isi_value SET batch_group=?,isi_value=?,normal_value=? WHERE id=?",
                              {b.batchGroup,isi,normal,metadata->value("id")},m,1);
        }
        if (b.metadataEdited)
            return db.execute("INSERT INTO isi.isi_value(batch_group,isi_value,normal_value) VALUES(?,?,?)",{b.batchGroup,b.isiValue,b.normalValue},m,1);
        return true;
    },message);
}

bool OriginalReagentEditor::remove(const QString &kind, qint64 id, QString *message)
{
    QString file=informationFile,table;
    if (kind=="reagent")
    {
        table="reagent_list"; const Row *row=byId(state_.reagents,id);
        if (!row || row->value("fixed").toBool()) return fail(message,QStringLiteral("内置试剂或不存在的记录不能删除。"));
        const QString name=row->value("reagent_name").toString();
        if (projectUses(name,false,message)) return fail(message,QStringLiteral("原项目仍引用该试剂，请先解除引用。"));
        for (const auto &hole : state_.holes) if (hole.value("reagent_name").toString()==name) return fail(message,QStringLiteral("原孔位仍引用该试剂。"));
        for (const auto &barcode : state_.barcodes) if (barcode.value("reagent_name").toString()==name) return fail(message,QStringLiteral("原条码仍引用该试剂。"));
    }
    else if (kind=="bottle")
    {
        file=bottleFile; table="reagent_bottle_type"; const Row *row=byId(state_.bottles,id);
        if (!row) return fail(message,QStringLiteral("原瓶型不存在。"));
        const QString name=row->value("bottle_type_name").toString();
        for (const auto &hole : state_.holes) if (hole.value("bottle_type").toString()==name) return fail(message,QStringLiteral("原孔位仍引用该瓶型。"));
        for (const auto &barcode : state_.barcodes) if (barcode.value("bottle_type").toString()==name) return fail(message,QStringLiteral("原条码仍引用该瓶型。"));
    }
    else if (kind=="barcode") table="reagent_barcode";
    else if (kind=="batch")
    {
        const Row *row=byId(state_.batchGroups,id);
        if (!row) return fail(message,QStringLiteral("原试剂组不存在。"));
        const QString group=row->value("batch_group").toString(),projectName=row->value("project_name").toString();
        if (group=="Default" || group=="None" || group=="PTFbg") return fail(message,QStringLiteral("原内置试剂组不能删除。"));
        for (const auto &project : projects_)
            if (project.name==projectName && project.originalBaseValues.value("is_related").toInt()>0)
            {
                int others=0;
                for (const auto &candidate : state_.batchGroups)
                    if (candidate.value("id").toLongLong()!=id && candidate.value("project_name").toString()==projectName &&
                        candidate.value("batch_group").toString()!="Default" && candidate.value("batch_group").toString()!="PTFbg") ++others;
                if (!others) return fail(message,QStringLiteral("项目已启用组分管理，最后一个实际试剂组不能删除。"));
            }
        // 原校准库按项目/批组名关联，不能仅删除组表留下悬空引用。
        Database calibration;
        if (!calibration.open(root_,"result/calibration.db",false,message)) return false;
        QSqlQuery references(calibration.db);
        if (!references.prepare("SELECT 1 FROM calibration_batch_group WHERE project_name=? AND batch_group=? LIMIT 1")) return fail(message,references.lastError().text());
        references.addBindValue(projectName); references.addBindValue(group);
        if (!references.exec()) return fail(message,references.lastError().text());
        if (references.next()) return fail(message,QStringLiteral("原校准记录仍引用该试剂组，请先解除引用。"));
        return mutate(informationFile,QString(),[&](Database &db,QString *m)
        {
            if (!db.execute("DELETE FROM reagent_batch_group WHERE id=?",{id},m,1)) return false;
            int remaining=0;
            for (const auto &other : state_.batchGroups) if (other.value("id").toLongLong()!=id && other.value("batch_group").toString()==group) ++remaining;
            if (remaining) return true;
            if (!attachIsi(db,root_,m)) return false;
            return db.execute("DELETE FROM isi.isi_value WHERE batch_group=?",{group},m);
        },message);
    }
    else return fail(message,QStringLiteral("原试剂删除类型不支持。"));
    return mutate(file,QString(),[&](Database &db,QString *m){ return db.execute(QStringLiteral("DELETE FROM %1 WHERE id=?").arg(table),{id},m,1); },message);
}
