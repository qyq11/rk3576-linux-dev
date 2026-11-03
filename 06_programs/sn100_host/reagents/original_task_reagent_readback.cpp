#include "reagent_store.h"
#include "original_reagent_editor.h"
#include "original_task_reagent_display.h"
#include "projects/original_project_copy.h"
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QSet>
#include <QThread>
#include <cmath>

namespace
{
const QString informationFile=QStringLiteral("db/userSet/reagent_Information.db");
const QString bottleFile=QStringLiteral("db/systemInfo/reagent_Bottle_Type.db");
const QString settingFile=QStringLiteral("db/userSet/reagent_Setting.db");
const QString isiFile=QStringLiteral("db/userSet/isi_Value.db");
const QStringList reagentFiles{informationFile,bottleFile,settingFile};

bool fail(QString *message,const QString &reason)
{
    if (message) *message=reason;
    return false;
}

bool sameValue(const QVariant &first,const QVariant &second)
{
    return first.userType()==second.userType() && first.isNull()==second.isNull() && first==second;
}

bool sameNumber(double first,double second)
{
    return first==second || (std::isnan(first) && std::isnan(second));
}

qint64 idByName(const OriginalBusiness::Rows &rows,const QString &key,const QString &name)
{
    for (const auto &row : rows) if (row.value(key).toString()==name) return row.value(QStringLiteral("id")).toLongLong();
    return 0;
}

bool afterVersionsMatch(const OriginalTaskReagents::Commit &receipt,QString *message)
{
    if (receipt.afterVersions.size()!=reagentFiles.size())
        return fail(message,QStringLiteral("任务回执没有完整三库after版本，未采用缓存。"));
    for (const auto &file : reagentFiles)
    {
        const QString expected=receipt.afterVersions.value(file);
        if (expected.isEmpty()) return fail(message,QStringLiteral("任务回执after版本缺失：")+file);
        const QString current=OriginalProjectRepository::databaseFingerprint(QDir(receipt.root).filePath(file),message);
        if (current.isEmpty()) return false;
        if (current!=expected)
            return fail(message,QStringLiteral("任务已提交，但专用重读时原三库已变化，未把旧after交给当前页面：")+file);
    }
    return true;
}

// 原编辑器没有无恢复的项目初始化口。只复用仍可逐文件证明的项目/ISI缓存；
// 有变更便失败，不由任务回执触发普通初始化或项目复制恢复。
bool cachedMetadataMatches(const QString &root,const ProjectRecords &projects,
                           const QMap<QString,QString> &versions,QString *message)
{
    QSet<QString> expected;
    for (const auto &file : reagentFiles)
    {
        if (versions.value(file).isEmpty())
            return fail(message,QStringLiteral("原编辑器尚无完整缓存版本，任务回执未临时补造。"));
        expected.insert(file);
    }
    expected.insert(isiFile);
    QSet<QString> projectFiles;
    for (const auto &project : projects)
    {
        const QString file=project.originalFileName;
        const QString relative=QStringLiteral("project/")+file;
        if (project.originalRoot!=root || file.isEmpty() || file!=QFileInfo(file).fileName() ||
            file.contains('/') || file.contains('\\') || projectFiles.contains(file) ||
            project.originalFingerprint.isEmpty() || versions.value(relative)!=project.originalFingerprint)
            return fail(message,QStringLiteral("原编辑器的项目缓存身份或版本不完整，未重新猜测项目。"));
        projectFiles.insert(file);
        expected.insert(relative);
    }
    if (expected.size()!=versions.size())
        return fail(message,QStringLiteral("原编辑器缓存版本集合不对应本次项目及ISI资料。"));
    for (auto entry=versions.cbegin();entry!=versions.cend();++entry)
    {
        if (!expected.contains(entry.key()) || entry.value().isEmpty())
            return fail(message,QStringLiteral("原编辑器缓存含未核实的资料版本。"));
        if (reagentFiles.contains(entry.key())) continue; // 三库由receipt.after单独逐文件核对。
        const QString current=OriginalProjectRepository::databaseFingerprint(QDir(root).filePath(entry.key()),message);
        if (current.isEmpty()) return false;
        if (current!=entry.value())
            return fail(message,QStringLiteral("任务已提交，但项目或ISI资料不再对应原缓存，显示保持待核：")+entry.key());
    }
    const QDir directory(QDir(root).filePath(QStringLiteral("project")));
    const QFileInfo directoryInfo(directory.absolutePath());
    if (!directoryInfo.isDir() || directoryInfo.isSymLink() || !directory.isReadable())
        return fail(message,QStringLiteral("项目目录不可读或类型不符，未推定为空项目缓存。"));
    QSet<QString> actual;
    // 与07实际OriginalProjectRepository::readProjects一致：所有大小写.db文件，
    // 不沿用N纯计数读取口的formula过滤规则。
    const auto entries=directory.entryInfoList(QDir::Files|QDir::NoDotAndDotDot|QDir::System,QDir::Name);
    for (const auto &entry : entries)
        if (entry.fileName().endsWith(QStringLiteral(".db"),Qt::CaseInsensitive))
        {
            if (entry.isSymLink() || !entry.isFile())
                return fail(message,QStringLiteral("项目文件存在符号链接或类型变化，任务未采用旧缓存。"));
            actual.insert(entry.fileName());
        }
    return actual==projectFiles || fail(message,QStringLiteral("任务重读期间项目文件有增删，未沿用不完整缓存。"));
}

bool projectedAfterMatches(const OriginalReagentState &state,const ReagentSnapshot &snapshot,
                           const OriginalTaskReagents::Commit &receipt,QString *message)
{
    if (state.root!=receipt.root || state.settingTable!=receipt.settingTable || state.versions!=receipt.afterVersions ||
        !snapshot.originalProfile || snapshot.originalRoot!=receipt.root ||
        state.holes.size()!=receipt.afterPlate.rows.size() || snapshot.holes.size()!=state.holes.size())
        return fail(message,QStringLiteral("任务after整盘、三库或页面投影不属于同一原资料。"));
    QMap<qint64,const OriginalTaskReagents::TypedRow *> after;
    for (const auto &row : receipt.afterPlate.rows)
    {
        bool ok=false;
        const qint64 id=row.values.value(QStringLiteral("id")).toLongLong(&ok);
        if (!ok || after.contains(id)) return fail(message,QStringLiteral("任务after行身份重复或无法读取。"));
        after.insert(id,&row);
    }
    QMap<qint64,const ReagentHole *> projected;
    QSet<QString> pageNames;
    for (const auto &hole : snapshot.holes)
    {
        if (projected.contains(hole.id) || pageNames.contains(hole.name))
            return fail(message,QStringLiteral("任务页面投影孔身份重复，未合并整盘记录。"));
        projected.insert(hole.id,&hole); pageNames.insert(hole.name);
    }
    for (const auto &row : state.holes)
    {
        bool ok=false;
        const qint64 id=row.value(QStringLiteral("id")).toLongLong(&ok);
        const auto *saved=after.value(id,nullptr);
        const auto *hole=projected.value(id,nullptr);
        if (!ok || !saved || !hole || row.keys()!=hole->originalValues.keys() ||
            hole->originalFingerprint!=receipt.afterVersions.value(settingFile))
            return fail(message,QStringLiteral("任务专用投影缺少唯一原孔或保存版本。"));
        // Repository读取的是已知列投影，不将它称为SELECT *完整行。
        // 未知列已经由verifyDisplayCommit全列链及前后整库版本绑定。
        for (auto field=row.cbegin();field!=row.cend();++field)
            if (!saved->values.contains(field.key()) || !sameValue(field.value(),saved->values.value(field.key())) ||
                !sameValue(field.value(),hole->originalValues.value(field.key())))
                return fail(message,QStringLiteral("任务专用重读的原列/Qt类型/NULL不对应完整after：")+field.key());
        if (hole->name!=row.value(QStringLiteral("hole_name")).toString().toUpper() ||
            hole->area!=row.value(QStringLiteral("area")).toString() ||
            hole->reagentId!=idByName(state.reagents,QStringLiteral("reagent_name"),row.value(QStringLiteral("reagent_name")).toString()) ||
            hole->bottleId!=idByName(state.bottles,QStringLiteral("bottle_type_name"),row.value(QStringLiteral("bottle_type")).toString()) ||
            hole->originalBatch!=row.value(QStringLiteral("reagent_batch")).toString() ||
            hole->expiry!=row.value(QStringLiteral("out_date")).toString() ||
            hole->changedAt!=row.value(QStringLiteral("datetime")).toString() ||
            hole->priority!=row.value(QStringLiteral("priority")).toInt() ||
            !sameNumber(hole->amountUl,row.value(QStringLiteral("amount_uL")).toDouble()) ||
            !sameNumber(hole->remainingUl,row.value(QStringLiteral("used_ul")).toDouble()))
            return fail(message,QStringLiteral("任务页面已知字段的原转换投影不一致。"));
    }
    return true;
}
}

bool OriginalReagentEditor::loadTaskReagentCommit(const OriginalTaskReagents::Commit &receipt,
    ReagentSnapshot *snapshot,ReagentProjects *choices,QString *message)
{
    if (snapshot) *snapshot=ReagentSnapshot();
    if (choices) choices->clear();
    if (!snapshot || !choices) return fail(message,QStringLiteral("任务专用重读输出为空。"));
    if (!OriginalTaskReagents::verifyDisplayCommit(receipt,message)) return false;
    if (root_.isEmpty() || root_!=receipt.root || state_.root!=root_ || state_.settingTable!=receipt.settingTable)
        return fail(message,QStringLiteral("任务专用读取的编辑器根或孔表已变化。"));
    if (!cachedMetadataMatches(root_,projects_,versions_,message) || !afterVersionsMatch(receipt,message)) return false;
    OriginalReagentState observed;
    if (!repository_.load(&observed,message)) return false; // 不initialize，不调用普通Editor::load或recover。
    if (observed.versions!=receipt.afterVersions || observed.settingTable!=receipt.settingTable)
        return fail(message,QStringLiteral("任务after与实际原三库读取版本或孔表不同。"));
    QMap<QString,QString> adoptedVersions=versions_;
    for (auto entry=receipt.afterVersions.cbegin();entry!=receipt.afterVersions.cend();++entry)
        adoptedVersions[entry.key()]=entry.value();
    ReagentSnapshot projected;
    ReagentProjects projectedChoices;
    projectSnapshot(observed,isiRows_,projects_,adoptedVersions,&projected,&projectedChoices);
    if (!projectedAfterMatches(observed,projected,receipt,message) ||
        !cachedMetadataMatches(root_,projects_,adoptedVersions,message) || !afterVersionsMatch(receipt,message)) return false;
    // 此对象是Store持有的临时值副本。完成所有证明后才改本副本的缓存。
    state_=observed; versions_=adoptedVersions;
    *snapshot=projected; *choices=projectedChoices;
    return true;
}

void ReagentStore::loadOriginalTaskReagentCommit(OriginalTaskReagents::Commit receipt)
{
    bool verified=false;
    ReagentSnapshot snapshot;
    ReagentProjects projects;
    QString message;
    do
    {
        // onWorker失败会发普通error；本专用入口始终只回交带原receipt的值信号。
        if (QThread::currentThread()!=thread())
        { message=QStringLiteral("任务回执未排入所属存储线程，未读取资料或采用缓存。"); break; }
        if (!ready_ || !originalEditor_ || receipt.root!=originalEditor_->root())
        { message=QStringLiteral("任务回执对应的原编辑器未就绪或工作根已变化。"); break; }
        if (!OriginalTaskReagents::verifyDisplayCommit(receipt,&message)) break;
        QLockFile project(QDir(receipt.root).filePath(QStringLiteral(".sn100-original-project-write.lock")));
        QLockFile reagent(QDir(receipt.root).filePath(QStringLiteral(".sn100-original-reagent-write.lock")));
        project.setStaleLockTime(0); reagent.setStaleLockTime(0);
        if (!project.tryLock(0) || !reagent.tryLock(0))
        { message=QStringLiteral("任务已提交，但专用读取的项目/试剂锁被占用，显示待核。"); break; }
        if (!OriginalProjectCopy::noPending(receipt.root,&message) || !afterVersionsMatch(receipt,&message)) break;
        std::unique_ptr<OriginalReagentEditor> staged(new OriginalReagentEditor(*originalEditor_));
        if (!staged->loadTaskReagentCommit(receipt,&snapshot,&projects,&message)) break;
        if (staged->deviceVersions()!=receipt.afterVersions ||
            !OriginalProjectCopy::noPending(receipt.root,&message) || !afterVersionsMatch(receipt,&message)) break;
        originalEditor_.swap(staged); // 不给旧缓存只换SHA；采用已完整读取且通过投影检查的整份缓存。
        verified=true;
    } while (false);
    if (!verified)
    {
        snapshot=ReagentSnapshot(); projects.clear();
        if (message.isEmpty()) message=QStringLiteral("任务专用读取未完成认证，已提交事实保留，显示待核。");
    }
    else message=QStringLiteral("任务提交后的完整凭据及三库资料已重读核对；页面动作/连接/revision采用仍另行核对。");
    // 两锁已释放；不发loaded、succeeded、normal error或设备通知，也不SQL重入接收方。
    emit originalTaskReagentReadback(receipt,verified,snapshot,projects,message);
}
