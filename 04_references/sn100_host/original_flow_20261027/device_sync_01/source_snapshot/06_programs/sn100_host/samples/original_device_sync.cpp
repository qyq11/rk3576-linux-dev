#include "original_device_sync.h"
#include <QDir>

namespace
{
bool fail(QString *message, const QString &text)
{
    if (message) *message=text;
    return false;
}
}

bool OriginalDeviceSync::initialize(const QString &root, QString *message)
{
    if (!repository_.initialize(root,message)) return false;
    root_=root; invalidate(); return true;
}

void OriginalDeviceSync::invalidate()
{
    ready_=false; waiting_=false; waitingForReagents_=false; projects_.clear(); projectIndex_=0;
    connection_=0;
}

bool OriginalDeviceSync::begin(quint64 connection, bool linuxNew, OriginalConfigurationStep *step, QString *message)
{
    if (!step || root_.isEmpty()) return fail(message,QStringLiteral("原配置同步未初始化。"));
    invalidate(); connection_=connection; linuxNew_=linuxNew;
    *step=OriginalConfigurationStep();
    if (!linuxNew)
    {
        // 原完整任务携带本次参数；Tu由设备请求时另行回应，不伪发新协议Up。
        ready_=true; step->complete=true; step->message=QStringLiteral("设备采用原完整任务协议，参数随任务下发。"); return true;
    }
    waiting_=true; waitingForReagents_=true;
    step->packets.append(QByteArrayLiteral("Ur"));
    step->message=QStringLiteral("正在读取设备实际试剂资料，随后同步原项目参数。");
    return true;
}

bool OriginalDeviceSync::reagentReplySaved(quint64 connection, OriginalConfigurationStep *step, QString *message)
{
    if (!step || connection!=connection_ || !waiting_ || !waitingForReagents_)
        return fail(message,QStringLiteral("该试剂返回不属于当前等待的配置同步。"));
    waitingForReagents_=false;
    if (!repository_.load(&projects_,message)) { invalidate(); return false; }
    return nextProject(step,message);
}

bool OriginalDeviceSync::nextProject(OriginalConfigurationStep *step, QString *message)
{
    *step=OriginalConfigurationStep();
    if (projectIndex_>=projects_.size())
    {
        waiting_=false; ready_=true; step->complete=true; step->message=QStringLiteral("原项目参数已发送并收到对应项目回读，实际试剂资料已保存。");
        return true;
    }
    const auto &project=projects_[projectIndex_];
    const QString path=QDir(root_).filePath(QStringLiteral("project/")+project.record.originalFileName);
    if (project.record.originalFingerprint!=OriginalProjectRepository::databaseFingerprint(path,message))
    { invalidate(); return fail(message,QStringLiteral("同步期间原项目被修改，未继续发送旧参数，请重新同步。")); }
    QByteArray update;
    if (!OriginalProjectWire::updatePayload(project.wire,&update,message)) { invalidate(); return false; }
    step->packets.append(update);
    step->packets.append("UP["+project.record.name.toLatin1()+']');
    step->message=QStringLiteral("正在同步项目%1（%2/%3），等待设备回读。").arg(project.record.name).arg(projectIndex_+1).arg(projects_.size());
    return true;
}

bool OriginalDeviceSync::projectReply(const QByteArray &payload, quint64 connection, OriginalConfigurationStep *step, QString *message)
{
    if (!step || connection!=connection_ || !waiting_ || waitingForReagents_ || projectIndex_>=projects_.size())
        return fail(message,QStringLiteral("当前连接没有等待这条项目回读，未覆盖原库。"));
    const auto sent=projects_[projectIndex_];
    OriginalProjectWire::Project received;
    if (!OriginalProjectWire::parseDeviceProject(payload,&received,message) ||
        !OriginalProjectDeviceRepository::matchesSentParameters(sent,received,message) ||
        !repository_.applyDeviceProject(payload,sent.record.name,sent.record.originalFingerprint,message))
    { invalidate(); return false; }
    // 保存设备额外字段后重新固定该文件指纹；开始前仍核对，不能沿用旧配置快照。
    projects_[projectIndex_].record.originalFingerprint=OriginalProjectRepository::databaseFingerprint(
        QDir(root_).filePath(QStringLiteral("project/")+sent.record.originalFileName),message);
    if (projects_[projectIndex_].record.originalFingerprint.isEmpty()) { invalidate(); return false; }
    ++projectIndex_;
    return nextProject(step,message);
}

bool OriginalDeviceSync::canStart(bool linuxNew, QString *message) const
{
    if (!linuxNew) return true;
    if (!linuxNew_ || !ready_) return fail(message,QStringLiteral("原项目或实际试剂同步尚未完成，请先连接并完成同步后再开始。"));
    OriginalProjectDeviceRepository currentRepository;
    QList<OriginalDeviceProject> currentProjects;
    if (!currentRepository.initialize(root_,message) || !currentRepository.load(&currentProjects,message)) return false;
    if (currentProjects.size()!=projects_.size())
        return fail(message,QStringLiteral("项目清单在同步后已变化，请重新同步后再开始。"));
    for (const auto &project : projects_)
    {
        bool unchanged=false;
        for (const auto &current : currentProjects)
            if (project.record.originalFileName==current.record.originalFileName &&
                project.record.originalFingerprint==current.record.originalFingerprint)
            { unchanged=true; break; }
        if (!unchanged)
            return fail(message,QStringLiteral("项目参数在同步后已变化，请重新同步，不使用设备旧参数开始。"));
    }
    return true;
}
