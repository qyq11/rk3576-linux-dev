#include "original_reagent_level.h"
#include "original_files/original_ini.h"
#include "projects/original_project_repository.h"
#include "projects/original_project_copy.h"
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QSet>
#include <algorithm>
#include <limits>

namespace OriginalLevels
{
namespace
{
bool fail(QString *message,const QString &reason)
{
    if (message) *message=reason;
    return false;
}
}

bool readProjectSnapshot(const QString &root,ProjectSnapshot *result,QString *message)
{
    if (!result) return fail(message,QStringLiteral("N项目快照输出为空。"));
    *result=ProjectSnapshot();
    if (!OriginalBusiness::workingRoot(root,message)) return false;
    // 复制清单未解除时，不能把未提交或未知状态的克隆当成普通项目。
    if (!OriginalProjectCopy::noPending(root,message)) return false;
    ProjectSnapshot loaded;
    const QString config=QDir(root).filePath(QStringLiteral("config.ini"));
    const QString configVersion=OriginalProjectRepository::databaseFingerprint(config,message);
    OriginalIni::Settings settings;
    QString actualKey;
    if (configVersion.isEmpty() || !OriginalIni::read(config,&settings,message) ||
        !OriginalIni::resolveKey(settings,QStringLiteral("sendConfig/sendSequence"),&actualKey,message)) return false;
    const QStringList sequence=actualKey.isEmpty() ? QStringList() : settings.values.value(actualKey).toStringList();
    loaded.versions.insert(QStringLiteral("config.ini"),configVersion);
    QDir directory(QDir(root).filePath(QStringLiteral("project")));
    if (!directory.exists() || !directory.isReadable() || QFileInfo(directory.absolutePath()).isSymLink())
        return fail(message,QStringLiteral("原项目目录不存在或为符号链接，未推定没有项目。"));
    directory.setFilter(QDir::Files|QDir::NoSymLinks);
    directory.setNameFilters({QStringLiteral("*.db")});
    const QStringList listing=directory.entryList();
    if (listing.size()>128) return fail(message,QStringLiteral("N所需项目清单超过128文件，未截断。"));
    int totalSteps=0;
    QSet<QString> projectNames;
    QSet<int> projectOrders;
    for (const QString &name : listing)
    {
        if (name.contains(QStringLiteral("formula"))) continue;
        const QString relative=QStringLiteral("project/")+name;
        const QString version=OriginalProjectRepository::databaseFingerprint(QDir(root).filePath(relative),message);
        if (version.isEmpty()) return false;
        OriginalBusiness::Database database;
        if (!database.open(root,relative,false,message)) return false;
        ProjectRecord project;
        project.originalRoot=root; project.originalFileName=name; project.originalFingerprint=version;
        QSqlQuery base(database.db);
        if (!base.exec(QStringLiteral("SELECT name,value FROM project_base"))) return fail(message,base.lastError().text());
        int baseRows=0;
        while (base.next())
        {
            if (++baseRows>100000) return fail(message,QStringLiteral("原项目基础键超过读取上限。"));
            const QString key=base.value(0).toString();
            if (project.originalBaseValues.contains(key)) return fail(message,QStringLiteral("原项目基础键重复，未猜测优先值。"));
            project.originalBaseValues.insert(key,base.value(1).toString());
        }
        if (base.lastError().isValid()) return fail(message,base.lastError().text());
        project.name=project.originalBaseValues.value(QStringLiteral("project_en_name"));
        bool orderOk=false;
        const int order=project.originalBaseValues.value(QStringLiteral("xuhao")).toInt(&orderOk);
        if (project.name.isEmpty() || !orderOk) return fail(message,QStringLiteral("原项目名称或xuhao顺序无效。"));
        const QString nameIdentity=project.name;
        if (projectNames.contains(nameIdentity) || projectOrders.contains(order))
            return fail(message,QStringLiteral("N项目实际名称或xuhao重复，未按文件名区分克隆或重复扣显示量。"));
        project.id=order;
        projectNames.insert(nameIdentity); projectOrders.insert(order);
        QSqlQuery steps(database.db);
        // 原SqlBase::getData使用select *而无ORDER BY；不能复用另一读口的xuhao,rowid排序。
        if (!steps.exec(QStringLiteral("SELECT * FROM project_reagent"))) return fail(message,steps.lastError().text());
        const QSqlRecord columns=steps.record();
        const int reagentColumn=columns.indexOf(QStringLiteral("reagent_name"));
        const int amountColumn=columns.indexOf(QStringLiteral("reagent_amount_ul"));
        if (reagentColumn<0 || amountColumn<0) return fail(message,QStringLiteral("原项目试剂表缺少名称或单次用量列。"));
        while (steps.next())
        {
            if (++totalSteps>32768) return fail(message,QStringLiteral("N项目试剂步骤超过32768行，未截断。"));
            ProjectActionFields step;
            step.insert(QStringLiteral("reagent_name"),steps.value(reagentColumn).toString());
            step.insert(QStringLiteral("reagent_amount_ul"),steps.value(amountColumn).toString());
            project.advanced.reagents.append(step);
        }
        if (steps.lastError().isValid()) return fail(message,steps.lastError().text());
        loaded.records.append(project); loaded.versions.insert(relative,version);
    }
    std::sort(loaded.records.begin(),loaded.records.end(),[](const ProjectRecord &a,const ProjectRecord &b)
    { return a.originalBaseValues.value(QStringLiteral("xuhao")).toInt()<b.originalBaseValues.value(QStringLiteral("xuhao")).toInt(); });
    if (!sequence.isEmpty())
    {
        const auto rank=[&sequence](const QString &name)
        {
            for (int i=0;i<sequence.size();++i) if (name.startsWith(sequence[i],Qt::CaseInsensitive)) return i;
            return std::numeric_limits<int>::max();
        };
        std::stable_sort(loaded.records.begin(),loaded.records.end(),[&rank](const ProjectRecord &a,const ProjectRecord &b)
        { return rank(a.name)<rank(b.name); });
    }
    if (listing!=directory.entryList()) return fail(message,QStringLiteral("读取N项目快照期间文件清单变化。"));
    for (auto it=loaded.versions.cbegin();it!=loaded.versions.cend();++it)
        if (it.value()!=OriginalProjectRepository::databaseFingerprint(QDir(root).filePath(it.key()),message))
            return fail(message,QStringLiteral("读取N项目快照期间资料版本变化。"));
    if (!OriginalProjectCopy::noPending(root,message)) return false;
    loaded.orderVerified=true; // 表示按原SQL/排序读取，不代表已经做过实际设备运行核对。
    *result=loaded;
    return true;
}
}
