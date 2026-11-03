#include "original_reagent_level.h"
#include "projects/original_project_repository.h"
#include "projects/original_project_copy.h"
#include "original_files/original_ini.h"
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QSet>
#include <cmath>

namespace OriginalLevels
{
namespace
{
const QString informationFile=QStringLiteral("db/userSet/reagent_Information.db");
const QString bottleFile=QStringLiteral("db/systemInfo/reagent_Bottle_Type.db");
const QString settingFile=QStringLiteral("db/userSet/reagent_Setting.db");

bool fail(QString *message, const QString &reason)
{
    if (message) *message=reason;
    return false;
}

bool sameNumber(double first, double second)
{
    return first==second || (std::isnan(first) && std::isnan(second));
}

bool sameCapacity(const OriginalRuntime::ReagentCapacityResult &first,
                  const OriginalRuntime::ReagentCapacityResult &second)
{
    return first.available && second.available && first.source==second.source &&
        first.fileState==second.fileState && first.matrixState==second.matrixState &&
        first.actualModel==second.actualModel && first.relativeFile==second.relativeFile &&
        first.fingerprint==second.fingerprint && sameNumber(first.rawHeightMm,second.rawHeightMm) &&
        first.quantizedHeightHundredths==second.quantizedHeightHundredths &&
        sameNumber(first.quantizedHeightMm,second.quantizedHeightMm) &&
        first.adjustedHeightAvailable==second.adjustedHeightAvailable &&
        (!first.adjustedHeightAvailable || sameNumber(first.adjustedHeightMm,second.adjustedHeightMm)) &&
        first.matrixMaximumMicrolitres==second.matrixMaximumMicrolitres &&
        sameNumber(first.matrixVolumeMicrolitres,second.matrixVolumeMicrolitres) &&
        sameNumber(first.volumeMicrolitres,second.volumeMicrolitres) &&
        sameNumber(first.displayVolumeMicrolitres,second.displayVolumeMicrolitres);
}

bool sameCapacityInput(const OriginalRuntime::ReagentCapacityInput &first,
                       const OriginalRuntime::ReagentCapacityInput &second)
{
    const auto &a=first.configuredBottle;
    const auto &b=second.configuredBottle;
    return first.hole==second.hole && first.needle==second.needle && first.area==second.area &&
        first.reportedBottle==second.reportedBottle && sameNumber(first.rawHeightMm,second.rawHeightMm) &&
        a.hole==b.hole && a.name==b.name && a.area==b.area &&
        sameNumber(a.bottomDiameterMm,b.bottomDiameterMm) && sameNumber(a.angleDegrees,b.angleDegrees) &&
        sameNumber(a.maximumMicrolitres,b.maximumMicrolitres);
}

bool sameRuntime(const CurrentHoleRuntime &first, const CurrentHoleRuntime &second)
{
    return first.available==second.available && first.root==second.root && first.hole==second.hole &&
        first.reagent==second.reagent && first.bottle==second.bottle && first.area==second.area &&
        first.reagentType==second.reagentType && first.state==second.state &&
        first.settingFingerprint==second.settingFingerprint && first.bottleFingerprint==second.bottleFingerprint &&
        first.informationFingerprint==second.informationFingerprint && first.revision==second.revision &&
        first.maximumSource==second.maximumSource &&
        sameNumber(first.maximumMicrolitres,second.maximumMicrolitres) &&
        sameNumber(first.amountMicrolitres,second.amountMicrolitres) &&
        sameNumber(first.usedMicrolitres,second.usedMicrolitres);
}

bool samePreparedResult(const PreparedObservation &first, const PreparedObservation &second)
{
    return first.holeId==second.holeId && first.formulaText==second.formulaText &&
        first.amountText==second.amountText && first.amountInteger==second.amountInteger &&
        first.previousUsedText==second.previousUsedText && first.usedText==second.usedText &&
        first.updateUsed==second.updateUsed && sameCapacityInput(first.capacityInput,second.capacityInput) &&
        sameCapacity(first.capacity,second.capacity) && sameRuntime(first.before,second.before) &&
        sameRuntime(first.after,second.after) &&
        first.selectedHoleTests.available==second.selectedHoleTests.available &&
        first.selectedHoleTests.tests==second.selectedHoleTests.tests &&
        first.selectedHoleTests.reason==second.selectedHoleTests.reason;
}

bool fingerprintMatches(const QString &root, const QString &relative, const QString &expected, QString *message)
{
    if (expected.isEmpty()) return fail(message,QStringLiteral("N保存缺少原文件版本：")+relative);
    const QString current=OriginalProjectRepository::databaseFingerprint(QDir(root).filePath(relative),message);
    if (current.isEmpty()) return false;
    return current==expected || fail(message,QStringLiteral("N到达后原资料已变化，未用旧观测覆盖：")+relative);
}

bool themeMatches(const Context &context, QString *message)
{
    const QString file=QStringLiteral("theme_config.ini");
    if (!fingerprintMatches(context.root,file,context.themeFingerprint,message)) return false;
    OriginalIni::Settings settings;
    QString modelKey, countKey;
    if (!OriginalIni::read(QDir(context.root).filePath(file),&settings,message) ||
        !OriginalIni::resolveKey(settings,QStringLiteral("theme/DriverModel"),&modelKey,message) ||
        !OriginalIni::resolveKey(settings,QStringLiteral("theme/ReagentPanelChannelCount"),&countKey,message)) return false;
    bool ok=false;
    // 原仓库在缺孔数键时选择20位；机型仍须来自已经核实的显式主题值。
    const int count=(countKey.isEmpty() ? QVariant(20) : settings.values.value(countKey)).toInt(&ok);
    if (modelKey.isEmpty() || settings.values.value(modelKey).toString()!=context.configuredModel ||
        !ok || count!=context.configuredPositions)
        return fail(message,QStringLiteral("N对应的主题机型或试剂位配置已不一致，未选择其他表保存。"));
    return fingerprintMatches(context.root,file,context.themeFingerprint,message);
}

bool projectsMatch(const PreparedObservation &prepared, QString *message)
{
    const auto &snapshot=prepared.projects;
    const QString &root=prepared.context.root;
    // 已持项目、试剂写锁；提交前的每次复核都不能绕过待核复制。
    if (!OriginalProjectCopy::noPending(root,message)) return false;
    if (!snapshot.orderVerified || snapshot.versions.value(QStringLiteral("config.ini")).isEmpty())
        return fail(message,QStringLiteral("N保存缺少已核实的原项目顺序或排序配置版本。"));
    QSet<QString> expectedFiles, expectedKeys, projectNames;
    QSet<int> projectOrders;
    expectedKeys.insert(QStringLiteral("config.ini"));
    for (const auto &project : snapshot.records)
    {
        const QString file=project.originalFileName;
        const QString key=QStringLiteral("project/")+file;
        bool orderOk=false;
        const int order=project.originalBaseValues.value(QStringLiteral("xuhao")).toInt(&orderOk);
        if (file.isEmpty() || file!=QFileInfo(file).fileName() || file.contains('/') || file.contains('\\') ||
            file.contains(QStringLiteral("formula")) || expectedFiles.contains(file) ||
            project.originalRoot!=root || project.originalFingerprint.isEmpty() ||
            snapshot.versions.value(key)!=project.originalFingerprint ||
            project.name.isEmpty() || project.originalBaseValues.value(QStringLiteral("project_en_name"))!=project.name ||
            !orderOk || project.id!=order || projectNames.contains(project.name) || projectOrders.contains(order))
            return fail(message,QStringLiteral("N保存的原项目文件集合、工作根或文件版本不完整。"));
        expectedFiles.insert(file);
        expectedKeys.insert(key);
        projectNames.insert(project.name); projectOrders.insert(order);
    }
    if (snapshot.versions.size()!=expectedKeys.size())
        return fail(message,QStringLiteral("N项目版本表不对应本次完整项目集合，未忽略额外或缺失文件。"));
    for (auto version=snapshot.versions.cbegin();version!=snapshot.versions.cend();++version)
        if (!expectedKeys.contains(version.key()) || version.value().isEmpty())
            return fail(message,QStringLiteral("N项目版本表包含未确认的路径或空版本。"));

    const QString directoryPath=QDir(root).filePath(QStringLiteral("project"));
    const QFileInfo directoryInfo(directoryPath);
    const QDir directory(directoryPath);
    if (directoryInfo.isSymLink() || !directoryInfo.isDir() || !directory.isReadable())
        return fail(message,QStringLiteral("原项目目录不可读或类型不符，未把读取失败当作空项目集合。"));
    QSet<QString> actualFiles;
    // 与原loadAllProject一致：仅*.db普通文件、排除符号链接和formula专用库。
    const QStringList entries=directory.entryList({QStringLiteral("*.db")},QDir::Files|QDir::NoSymLinks,QDir::Name);
    for (const auto &file : entries)
        if (!file.contains(QStringLiteral("formula"))) actualFiles.insert(file);
    if (actualFiles!=expectedFiles)
        return fail(message,QStringLiteral("N到达后原项目文件有增删，未沿用旧项目步骤或顺序保存。"));
    for (auto version=snapshot.versions.cbegin();version!=snapshot.versions.cend();++version)
        if (!fingerprintMatches(root,version.key(),version.value(),message)) return false;
    return true;
}

bool capacityMatches(const PreparedObservation &prepared, QString *message)
{
    OriginalRuntime::ReagentCapacityResult current;
    if (!OriginalRuntime::calculateReagentCapacity(prepared.context.root,prepared.context.actualModel,
                                                   prepared.capacityInput,&current,message)) return false;
    // 容量指纹是v2读取的原字节SHA，不能与包含SQLite旁文件的通用指纹混比。
    if (!sameCapacity(prepared.capacity,current))
        return fail(message,QStringLiteral("N准备后容量文件存在状态、版本或换算结果已变化，未改用其他模型保存。"));
    return true;
}
}

bool saveObservation(const PreparedObservation &prepared, const std::function<bool()> &stillCurrent,
                     bool *committed, QString *savedFingerprint, QString *message)
{
    if (message) message->clear();
    if (committed) *committed=false;
    if (savedFingerprint) savedFingerprint->clear();
    if (!committed || !savedFingerprint)
        return fail(message,QStringLiteral("N保存的提交事实或保存指纹输出为空，未写入。"));
    const auto &context=prepared.context;
    const auto &observed=prepared.observed;
    const QStringList files{informationFile,bottleFile,settingFile};
    const QString table=context.configuredPositions==20 ? QStringLiteral("reagent_setting") : QStringLiteral("reagent_setting_6600");
    if (!context.packetId || !context.connection || !context.permissionRevision || context.root.isEmpty() ||
        !QDir::isAbsolutePath(context.root) || context.actualModel.isEmpty() ||
        context.actualModel!=context.configuredModel || context.themeFingerprint.isEmpty() ||
        (context.configuredPositions!=20 && context.configuredPositions!=42) ||
        context.configuredPositions!=context.reportedPositions || context.mainStatus<0 || context.mainStatus>6 ||
        context.capacityCalibrationActive || prepared.received.kind!=PacketKind::Reagent ||
        prepared.received.linuxNew!=context.linuxNew || observed.root!=context.root || observed.settingTable!=table ||
        observed.versions.size()!=files.size() || prepared.holeId<=0 ||
        prepared.updateUsed!=(context.mainStatus!=5))
        return fail(message,QStringLiteral("N保存的连接、原工作根、机型、状态、目标表或三库版本合同不完整。"));
    for (const auto &file : files)
        if (observed.versions.value(file).isEmpty())
            return fail(message,QStringLiteral("N保存缺少到达时原库版本：")+file);
    const auto permitted=[&]()
    {
        return (stillCurrent && stillCurrent()) ||
            fail(message,QStringLiteral("N到达时的连接、运行快照或保存许可已失效，未继续写入。"));
    };
    const auto versionsMatch=[&](bool includeSetting)
    {
        if (!permitted()) return false;
        for (const auto &file : files)
            if ((includeSetting || file!=settingFile) &&
                !fingerprintMatches(context.root,file,observed.versions.value(file),message)) return false;
        if (!themeMatches(context,message) || !projectsMatch(prepared,message)) return false;
        return permitted();
    };
    if (!permitted() || !OriginalBusiness::workingRoot(context.root,message)) return false;
    QLockFile projectLock(QDir(context.root).filePath(QStringLiteral(".sn100-original-project-write.lock")));
    QLockFile reagentLock(QDir(context.root).filePath(QStringLiteral(".sn100-original-reagent-write.lock")));
    projectLock.setStaleLockTime(0); reagentLock.setStaleLockTime(0);
    if (!projectLock.tryLock(0) || !reagentLock.tryLock(0))
        return fail(message,QStringLiteral("原项目或试剂正在保存，N观测未覆盖当前操作。"));
    if (!versionsMatch(true) || !capacityMatches(prepared,message)) return false;

    OriginalBusiness::Database database;
    if (!database.open(context.root,settingFile,true,message) || !database.begin(message)) return false;
    const auto rollback=[&]() { database.rollback(); return false; };
    if (!versionsMatch(true)) return rollback();
    OriginalReagentRepository repository;
    OriginalReagentState current;
    if (!repository.initialize(context.root,message) || !repository.load(&current,message)) return rollback();
    if (current.root!=context.root || current.settingTable!=table || current.versions!=observed.versions)
    {
        fail(message,QStringLiteral("进入N写事务后原试剂表或版本已变化，未保存旧观测。"));
        return rollback();
    }
    PreparedObservation verified;
    if (!prepareObservation(current,prepared.received,context,prepared.before,prepared.projects,&verified,message)) return rollback();
    if (!samePreparedResult(prepared,verified))
    {
        fail(message,QStringLiteral("N事务内复核结果与准备时的字段、容量或运行快照不一致，未保存。"));
        return rollback();
    }
    if (!versionsMatch(true)) return rollback();

    // 原Table路径把默认精度液量文本转成整数amount_uL；used_ul仍保留原液量文本。
    QString assignments=QStringLiteral("formula_level=?,amount_uL=?");
    QVariantList arguments{verified.formulaText,verified.amountInteger};
    if (verified.updateUsed)
    {
        assignments+=QStringLiteral(",used_ul=?");
        arguments.append(verified.usedText);
    }
    arguments.append(verified.holeId);
    arguments.append(verified.received.hole);
    if (!database.execute(QStringLiteral("UPDATE \"%1\" SET %2 WHERE id=? AND hole_name=?").arg(table,assignments),
                          arguments,message,1)) return rollback();
    // 本事务已经改孔位库，提交前不把自身脏页当外部冲突；其他依赖继续核原版本。
    if (!versionsMatch(false) || !capacityMatches(prepared,message) || !permitted()) return rollback();
    if (!database.commit(message)) return rollback();
    *committed=true;
    database.db.close();
    QString fingerprintError;
    *savedFingerprint=OriginalProjectRepository::databaseFingerprint(QDir(context.root).filePath(settingFile),&fingerprintError);
    if (savedFingerprint->isEmpty())
        return fail(message,QStringLiteral("N观测已提交，但保存后的孔位指纹无法核对；不能按失败假定未写入：")+fingerprintError);
    if (!stillCurrent || !stillCurrent())
        return fail(message,QStringLiteral("N观测已提交，但连接或运行快照在提交期间变化，页面及当前设备结果仍待核对。"));
    if (message) *message=QStringLiteral("N观测指定字段已提交并取得保存指纹，页面重读及运行快照更新另行核对；未发送设备命令。");
    return true;
}
}
